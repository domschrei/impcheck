
#include <stdlib.h>
#include <stdbool.h>        // for bool, false, true
#include <stdio.h>          // for snprintf
#include <unistd.h>
#include "clause.h"
#include "hash.h"           // for hash_table_find, hash_table_delete_last_f...
#include "pointer_storage.h"
#include "secret.h"
#include "siphash.h"        // for siphash_digest, siphash_update
#include "sort.h"
#include "trusted_utils.h"  // for u64, trusted_utils_msgstr, MALLOB_UNLIKELY
#include "assert.h"
#include "check.h"
#include "confirm.h"

// Instantiate int_vec
#define TYPE int
#define TYPED(THING) int_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

// Instantiate i8_vec
#define TYPE signed char
#define TYPED(THING) i8_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

// Instantiate u64_vec
#define TYPE u64
#define TYPED(THING) u64_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

// Instantiate sig_vec
#define TYPE SIG_TYPE
#define TYPED(THING) sig_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

// The hash table where we keep all learned clauses.
// We use a power-of-two growth policy for fast lookups.
struct hash_table* clause_table;
// A plain vector where we keep all original problem clauses, indexed by their ID.
// Since input clauses are dense w.r.t. their IDs, this is faster and more
// space-efficient than also inserting them in the produced clauses hash table.  
struct u64_vec* input_clauses;

// Table of all variables with their current assignment (-1/0/1).
// We perform all LRUP checks using one big vector of all variable polarities,
// which is set and reset for each check. This allows for O(1) queries
// for a literal's assignment.
struct i8_vec* var_values;

// We remember the set variables in a stack to reset them later.
struct int_vec* assigned_units;

bool first_load = true;
bool check_model;
bool lenient;
u64 id_to_add = 1;
u64 nb_loaded_clauses = 0;
struct int_vec* clause_to_add;
bool loading = false;
SIG_TYPE last_f_sig;

struct siphash* siphash_f;

int* a_ptr;
int nb_assumptions = 0;

bool parsed_formula = false;
SIG_TYPE formula_signature;

bool valid = true;

struct hash_table* ht_signatures;

struct siphash* siphash_cls;

bool free_clause(u64 id, CLSTYPE cls) {
    bool original = id <= nb_loaded_clauses;
    // Do not delete original problem clauses to enable checking of a model
    if (original && check_model) return true;
#if IMPCHECK_COMPRESS
    // Avoid trying to free fake pointers with stored data
    if (ptr_storage_is_real_pointer(cls))
#else
    if (true)
#endif
        free(cls);
    if (original) {
        input_clauses->data[id-1] = 0;
    } else if (!hash_table_delete_last_found(clause_table)) {
        snprintf(trusted_utils_msgstr, 512, "Clause deletion: Hash table error for ID %lu", id);
        return false;
    }
    return true;
}

CLSTYPE fetch_clause(u64 id) {
    if (id <= nb_loaded_clauses) {
        return (CLSTYPE) input_clauses->data[id-1];
    }
    return hash_table_find(clause_table, id);
}

SIG_TYPE compute_clause_signature(u64 id, const int* lits, int nb_lits, u32 cidx) {
    siphash_reset(siphash_cls);
    siphash_update(siphash_cls, (u8*) &id, sizeof(u64));
    siphash_update(siphash_cls, (u8*) lits, nb_lits*sizeof(int));
    u8* fsig = hash_table_find(ht_signatures, cidx);
    if (MALLOB_UNLIKELY(!fsig)) {
        snprintf(trusted_utils_msgstr, 512, "No valid formula signature for cidx %i", cidx);
        valid = false;
    } else {
        siphash_update(siphash_cls, fsig, SIG_SIZE_BYTES);
    }
    siphash_pad(siphash_cls, 2);
    return siphash_digest(siphash_cls);
}

void print_error_clause(CLSTYPE cls, const char* descriptor) {
    printf("[IMPCHK %i] ERR %s:", getpid(), descriptor);
    FOR_LIT_IN_CLAUSE(cls, lit) printf(" %i", lit);
    printf("\n");
}
void print_error_clause_lits(const int* lits, int nb_lits, const char* descriptor) {
    printf("[IMPCHK %i] ERR %s:", getpid(), descriptor);
    for (int i = 0; i < nb_lits; i++) printf(" %i", lits[i]);
    printf("\n");
}
void print_error_clause_full(u64 base_id, const int* lits, int nb_lits, const u64* hints, int nb_hints) {
    printf("[IMPCHK %i] ERR ID=%lu", getpid(), base_id);
    for (int i = 0; i < nb_lits; i++) {
        printf(" %i", lits[i]);
    }
    printf("\n");
    for (int i = 0; i < nb_hints; i++) {
        const u64 hint_id = hints[i];
        printf("[IMPCHK %i] ERR - cls ID=%lu", getpid(), hint_id);
        CLSTYPE cls = fetch_clause(hint_id);
        FOR_LIT_IN_CLAUSE(cls, lit) printf(" %i", lit);
        printf("\n");
    }
}

bool add_axiomatic_clause(u64 id, int* lits, int nb_lits) {
    CLSTYPE cls = clause_init(lits, nb_lits);
    bool ok = true;
    if (loading) {
        u64_vec_push(input_clauses, (u64) cls);
        assert(id == input_clauses->size);
    } else {
        ok = hash_table_insert(clause_table, id, cls);
    }
    if (!ok) {
        if (lenient) {
            // In lenient mode, ignore the addition if and only if the clauses
            // are syntactically equivalent (except for literal ordering).
            if (clauses_equivalent(fetch_clause(id), cls)) {
                ok = true;
            }
        }
        if (!ok) {
            snprintf(trusted_utils_msgstr, 512, "Insertion of clause %lu unsuccessful - already present?", id);
            print_error_clause(fetch_clause(id), "Present clause");
            print_error_clause_lits(lits, nb_lits, "Incoming clause");
        }
    }
    return ok;
}

signed char get_var_value(u32 v) {
    while (v >= var_values->size) i8_vec_push(var_values, 0);
    return var_values->data[v];
}
void set_var_value(u32 v, signed char val) {
    while (v >= var_values->size) i8_vec_push(var_values, 0);
    var_values->data[v] = val;
}

void reset_assignments(void) {
    for (u64 i = 0; i < assigned_units->size; i++)
        set_var_value(assigned_units->data[i], 0);
    int_vec_clear(assigned_units);
}

bool check_clause(u64 base_id, const int* lits, int nb_lits, const u64* hints, int nb_hints) {

    int_vec_reserve(assigned_units, nb_lits + nb_hints);
    // Assume the negation of each literal in the new clause
    for (int i = 0; i < nb_lits; i++) {
        const int var = lits[i] > 0 ? lits[i] : -lits[i];
        set_var_value(var, lits[i]>0 ? -1 : 1); // negated
        int_vec_push(assigned_units, var); // remember to reset later
    }

    // Traverse the provided hints to derive a conflict, i.e., the empty clause
    bool ok = true;
    for (int i = 0; i < nb_hints; i++) {

        // Find the clause for this hint
        const u64 hint_id = hints[i];
        const CLSTYPE cls = fetch_clause(hint_id);
        if (MALLOB_UNLIKELY(!cls)) {
            // ERROR - hint not found
            snprintf(trusted_utils_msgstr, 512, "Derivation %lu: hint %lu not found", base_id, hint_id);
            break;
        }

        // Interpret hint clause (should derive a new unit clause)
        int new_unit = 0;
        FOR_LIT_IN_CLAUSE(cls, lit) {
            const int var = lit > 0 ? lit : -lit;
            if (get_var_value(var) == 0) {
                // Literal is unassigned
                if (MALLOB_UNLIKELY(new_unit != 0)) {
                    // ERROR - multiple unassigned literals in hint clause!
                    snprintf(trusted_utils_msgstr, 512, "Derivation %lu: hint %lu: multiple literals unassigned "
                        "- first %i then %i", base_id, hint_id, new_unit, lit);
                    print_error_clause_full(base_id, lits, nb_lits, hints, nb_hints);
                    ok = false; break;
                }
                new_unit = lit;
                continue;
            }
            // Literal is fixed
            const bool sign = get_var_value(var) > 0;
            if (MALLOB_UNLIKELY(sign == (lit>0))) {
                // ERROR - clause is satisfied, so it is not a correct hint
                snprintf(trusted_utils_msgstr, 512, "Derivation %lu: hint %lu: literal %i is satisfied",
                    base_id, hint_id, lit);
                print_error_clause_full(base_id, lits, nb_lits, hints, nb_hints);
                ok = false; break;
            }
            // All OK - literal is false, thus (virtually) removed from the clause
        }
        if (!ok) break; // error detected - stop

        // NO unit derived?
        if (new_unit == 0) {
            // No unassigned literal in the clause && clause not satisfied
            // -> Empty clause derived.
            if (MALLOB_UNLIKELY(i+1 < nb_hints)) {
                // ERROR - not at the final hint yet!
                snprintf(trusted_utils_msgstr, 512, "Derivation %lu: empty clause produced at non-final hint %lu", base_id, hint_id);
                print_error_clause_full(base_id, lits, nb_lits, hints, nb_hints);
                break;
            }
            // Final hint produced empty clause - everything OK!
            reset_assignments();
            return true;
        }
        // Insert the new derived unit clause
        int var = new_unit > 0 ? new_unit : -new_unit;
        set_var_value(var, new_unit>0 ? 1 : -1);
        int_vec_push(assigned_units, var); // remember to reset later
    }

    // ERROR - something went wrong
    if (trusted_utils_msgstr[0] == '\0') {
        snprintf(trusted_utils_msgstr, 512, "Derivation %lu: no empty clause was produced", base_id);
        print_error_clause_full(base_id, lits, nb_lits, hints, nb_hints);
    }
    reset_assignments();
    return false;
}

bool check_and_add_clause(u64 id, int* lits, int nb_lits, const u64* hints, int nb_hints) {
    if (loading) {
        snprintf(trusted_utils_msgstr, 512, "Illegal clause addition during loading");
        return false;
    }
    if (!check_clause(id, lits, nb_lits, hints, nb_hints)) {
        return false;
    }
    return add_axiomatic_clause(id, lits, nb_lits);
}

bool validate_sat(int* model, u64 size) {

    // Still loading the formula?
    if (loading) {
        snprintf(trusted_utils_msgstr, 512, "SAT validation illegal - loading formula was not concluded");
        return false;
    }
    // Not executed with checking of models enabled?
    if (!check_model) {
        snprintf(trusted_utils_msgstr, 512, "SAT validation illegal - not executed to explicitly support this");
        return false;
    }

    // Check that all assumptions are satisfied by the model
    int aidx = 0;
    for (u32 midx = 0; midx < size; midx++) {
        int lit = model[midx];
        while (aidx < nb_assumptions && abs(a_ptr[aidx]) != abs(lit)) aidx++;
        if (aidx == nb_assumptions) break; // no more assumptions in the remaining model
        int asmpt = a_ptr[aidx];
        if (asmpt != lit) {
            snprintf(trusted_utils_msgstr, 512, "SAT validation: assumption %i broken by model lit %i", asmpt, lit);
            return false;
        }
    }

    // Check each original problem clause
    for (u64 id = 1; id <= nb_loaded_clauses; id++) {
        const CLSTYPE cls = fetch_clause(id);
        if (MALLOB_UNLIKELY(!cls)) {
            // ERROR - clause not found
            snprintf(trusted_utils_msgstr, 512, "SAT validation: original ID %lu not found", id);
            return false;
        }
        // Iterate over the literals of the clause
        bool satisfied = false;
        FOR_LIT_IN_CLAUSE(cls, lit) {
            const int var = lit>0 ? lit : -lit;
            if (MALLOB_UNLIKELY((u64) (var-1) >= size)) {
                // ERROR - model does not cover this variable
                snprintf(trusted_utils_msgstr, 512, "SAT validation: model does not cover variable %i", var);
                return false;
            }
            // Is the literal satisfied in the model?
            int modelLit = model[var-1];
            if (MALLOB_UNLIKELY(modelLit != var && modelLit != -var && modelLit != 0)) {
                // ERROR - clause not found
                snprintf(trusted_utils_msgstr, 512, "SAT validation: unexpected literal %i in assignment of variable %i", modelLit, var);
                return false;
            }
            if (modelLit == 0) {
                // The value of this variable allegedly does not matter,
                // so let us just assign the fitting value.
                // If this leads to an error, it does matter, which means that the specified model is wrong.
                modelLit = model[var-1] = lit;
            }
            if (modelLit == lit) {
                // Literal satisfied under the model satisfies the clause
                satisfied = true;
                break;
            }
        }
        // Clause NOT satisfied?
        if (MALLOB_UNLIKELY(!satisfied)) {
            // ERROR - unsatisfied clause(s) remain(s)
            snprintf(trusted_utils_msgstr, 512, "SAT validation: original clause %lu not satisfied", id);
            return false;
        }
    }
    // All original problem clauses are satisfied – correct model!
    return true;
}

bool validate_unsat(u64 id, const int* failed, int size) {
    if (loading) {
        snprintf(trusted_utils_msgstr, 512, "Illegal UNSAT validation during loading");
        return false;
    }

    // Copy failed assumptions into its own array so that we can manipulate it
    int* copy_failed = trusted_utils_malloc(size * sizeof(int));
    trusted_utils_copy_bytes((u8*) copy_failed, (u8*) failed, size * sizeof(int));
    sort_ints(copy_failed, size);

    // Check that all failed literals are assumptions of the current call
    int aidx = 0;
    for (int fidx = 0; fidx < size; fidx++) {
        int lit = copy_failed[fidx];
        while (aidx < nb_assumptions && a_ptr[aidx] != lit) aidx++;
        if (aidx == nb_assumptions) {
            // Failed literal was not one of the assumptions!
            snprintf(trusted_utils_msgstr, 512, "UNSAT validation: failed lit %i not an assumption!", lit);
            return false;
        }
    }

    // For convenience, flip the assumptions marked as failed to their failed units
    // and sort the sequence again.
    for (int i = 0; i < size; i++) copy_failed[i] *= -1;
    sort_ints(copy_failed, size);

    // Fetch referenced conclusion clause
    CLSTYPE cls = fetch_clause(id);
    if (!cls) {
        snprintf(trusted_utils_msgstr, 512, "UNSAT validation: ID %lu not found", id);
        return false;
    }
    // Convert to plain, sorted array of external literals
    struct int_vec* vec_cls = int_vec_init(16);
    FOR_LIT_IN_CLAUSE(cls, lit) int_vec_push(vec_cls, lit);
    sort_ints(vec_cls->data, vec_cls->size);

    // Make sure that each literal of the conclusion clause marks a failed unit.
    aidx = 0;
    int lit;
    bool ok = true;
    for (u32 cidx = 0; cidx < vec_cls->size; cidx++) {
        lit = vec_cls->data[cidx];
        while (aidx < size && copy_failed[aidx] != lit) aidx++;
        if (aidx == size) {
            ok = false;
            break;
        }
    }
    int_vec_free(vec_cls);

    if (!ok) {
        // Clause literal is not part of the assumptions marked as failed!
        snprintf(trusted_utils_msgstr, 512, "UNSAT validation: conclusion lit %i"
            " is not specified as a failed assumption!", lit);
        print_error_clause(cls, "Conclusion");
        print_error_clause_lits(copy_failed, size, "Failed lits");
        return false;
    }

    // Exact equivalence check of conclusion and failed assumptions - too strict?
    if (false) {
        // Construct a clause from the negated failed literals
        CLSTYPE cls_failed = clause_init(copy_failed, size);
#if !IMPCHECK_COMPRESS
        sort_ints(cls_failed, size);
#endif
        // Check syntactical equivalence of clauses
        if (!clauses_equivalent(cls, cls_failed)) {
            snprintf(trusted_utils_msgstr, 512,
                "UNSAT validation: failed lits not matching conclusion clause %lu!", id);
            print_error_clause(cls, "Conclusion");
            print_error_clause(cls_failed, "Failed lits");
            return false;
        }
        if (ptr_storage_is_real_pointer(cls_failed)) free(cls_failed);
    }

    free(copy_failed);
    return true;
}


void checker_init(bool opt_check_model, bool opt_lenient) {
    clause_table = hash_table_init(14);
    input_clauses = u64_vec_init(1024);
    clause_to_add = int_vec_init(512);
    var_values = i8_vec_init(512);
    assigned_units = int_vec_init(512);
    check_model = opt_check_model;
    lenient = opt_lenient;
    siphash_f = siphash_init(SECRET_KEY);
    ht_signatures = hash_table_init(6);
    siphash_cls = siphash_init(SECRET_KEY);
}

void checker_commit_formula_sig(SIG_TYPE f_sig) {
    if (!valid) return;
    // Store formula signature to validate later after loading
    formula_signature = f_sig;
    //sig_vec_push(signatures, formula_signature);
    valid = !loading;
    if (valid) loading = true;
}

void checker_load(int lit) {
    if (lit != 0) {
        int_vec_push(clause_to_add, lit);
        return;
    }
    int_vec_push(clause_to_add, 0);
    siphash_update(siphash_f, (u8*) clause_to_add->data, clause_to_add->size*sizeof(int));
    if (add_axiomatic_clause(id_to_add, clause_to_add->data, clause_to_add->size - 1)) {
        id_to_add++;
        int_vec_clear(clause_to_add);
    } else {
        valid = false;
    }
}

bool checker_end_load(int* assumptions, int size) {
    if (!valid) return false;

    if (!loading) {
        snprintf(trusted_utils_msgstr, 512, "END_LOAD received while not loading!");
        valid = false;
        return false;
    }
    if (clause_to_add->size > 0) {
        snprintf(trusted_utils_msgstr, 512, "literals left in unterminated clause");
        valid = false;
        return false;
    }

    last_f_sig = siphash_end_branch(siphash_f, 0);
    loading = false;
    nb_loaded_clauses = id_to_add-1;

    a_ptr = assumptions;
    nb_assumptions = size;
    sort_ints(a_ptr, nb_assumptions);

    // Check against provided signature
    valid = trusted_utils_equal_signatures(last_f_sig, formula_signature);
    if (!valid) {
        snprintf(trusted_utils_msgstr, 512, "Formula signature check failed");
        return false;
    }
    // Insert this signature in the hash table of formula signatures
    u8* newsig = trusted_utils_malloc(sizeof(SIG_TYPE));
    trusted_utils_copy_bytes(newsig, (u8*) &formula_signature, SIG_SIZE_BYTES);
    hash_table_insert(ht_signatures, nb_loaded_clauses, newsig);
    return valid;
}

bool checker_produce(unsigned long id, int* literals, int nb_literals,
    const unsigned long* hints, int nb_hints, SIG_TYPE* out_sig_or_null, u32* cidx_or_null) {
    if (!valid) return false;

    // compute signature if desired
    if (out_sig_or_null) {
        *out_sig_or_null = compute_clause_signature(id, literals, nb_literals, nb_loaded_clauses);
    }
    valid = valid && check_and_add_clause(id, literals, nb_literals, hints, nb_hints);
    if (valid && cidx_or_null) *cidx_or_null = nb_loaded_clauses;
    return valid;
}

bool checker_import(unsigned long id, int* literals, int nb_literals,
    SIG_TYPE signature_data, u32 cidx) {
    if (!valid) return false;

    // verify signature
    SIG_TYPE computed_sig = compute_clause_signature(id, literals, nb_literals, cidx);
    if (!valid) return false;
    if (!trusted_utils_equal_signatures(signature_data, computed_sig)) {
        valid = false;
        snprintf(trusted_utils_msgstr, 512, "Signature check of clause %lu from cidx %i failed", id, cidx);
        return false;
    }

    // signature verified - forward clause to checker as an axiom
    valid = valid && add_axiomatic_clause(id, literals, nb_literals);
    return valid;
}

bool checker_delete(const unsigned long* ids, int nb_ids) {
    if (!valid) return false;

    if (loading) {
        snprintf(trusted_utils_msgstr, 512, "Illegal clause deletion during loading");
        valid = false;
        return false;
    }
    for (int i = 0; i < nb_ids; i++) {
        u64 id = ids[i];
        CLSTYPE cls = fetch_clause(id);
        if (!cls) {
            snprintf(trusted_utils_msgstr, 512, "Clause deletion: ID %lu not found", id);
            valid = false;
            return false;
        }
        if (!free_clause(id, cls)) {
            valid = false;
            return false; // writes to "trusted_utils_msgstr" internally
        }
    }
    return valid;
}

bool checker_validate_unsat(u64 id, int* failed, int size, SIG_TYPE* out_signature_or_null) {
    if (!valid) return false;
    valid = validate_unsat(id, failed, size);
    if (!valid) {
        trusted_utils_log_err(trusted_utils_msgstr);
        return false;
    }
    if (out_signature_or_null)
        *out_signature_or_null = confirm_result(formula_signature, 20, size, failed);
    return true;
}

bool checker_validate_sat(int* model, u64 size, int* assumptions, u32 nb_assumptions, SIG_TYPE* out_signature_or_null) {
    if (!valid) return false;
    valid = validate_sat(model, size);
    if (!valid) {
        trusted_utils_log_err(trusted_utils_msgstr);
        return false;
    }
    if (out_signature_or_null)
        *out_signature_or_null = confirm_result(formula_signature, 10, nb_assumptions, assumptions);
    return true;
}

bool checker_valid(void) {return valid;}

u32 checker_get_nb_input_clauses(void) {
    return nb_loaded_clauses;
}
