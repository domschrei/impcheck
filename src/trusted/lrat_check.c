
#include <stdlib.h>
#include <stdbool.h>        // for bool, false, true
#include <stdio.h>          // for snprintf
#include "clausecompress.h"
#include "hash.h"           // for hash_table_find, hash_table_delete_last_f...
#include "pointer_storage.h"
#include "siphash.h"        // for siphash_digest, siphash_update
#include "trusted_utils.h"  // for u64, trusted_utils_msgstr, MALLOB_UNLIKELY
#include "assert.h"

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

bool check_model;
bool lenient;
u64 id_to_add = 1;
u64 nb_loaded_clauses = 0;
struct int_vec* clause_to_add;
bool done_loading = false;
bool unsat_proven = false;


int* clause_init(const int* data, int nb_lits) {
    int* cls = trusted_utils_calloc(nb_lits+1, sizeof(int));
    for (int i = 0; i < nb_lits; i++) cls[i] = data[i];
    cls[nb_lits] = 0;
    return cls;
}
u8* cclause_init(int* data, int nb_lits) {
    u8* out;
    int size = cc_prepare_clause_and_get_compressed_size(data, nb_lits);
    if (size <= 7) {
        u8 inlineCls[8] = {0};
        cc_compress_and_write_clause(data, nb_lits, size, inlineCls);
        out = ptr_storage_create(inlineCls);
    } else {
        out = trusted_utils_calloc(size, 1);
        cc_compress_and_write_clause(data, nb_lits, size, out);
    }
    return out;
}
struct cclause_view get_cclause_view(const u8** cls) {
    struct cclause_view view;
    const void* data = ptr_storage_get((const void**) cls);
    view = cc_get_compressed_view((const u8*) data);
    return view;
}

u8* fetch_clause(u64 id) {
    if (id <= nb_loaded_clauses) {
        return (u8*) input_clauses->data[id-1];
    }
    return hash_table_find(clause_table, id);
}
bool free_clause(u64 id, u8* cls) {
    bool original = id <= nb_loaded_clauses;
    // Do not delete original problem clauses to enable checking of a model
    if (original && check_model) return true;
    // Avoid trying to free fake pointers with stored data
    if (ptr_storage_is_real_pointer(cls)) free(cls);
    if (original) {
        input_clauses->data[id-1] = 0;
    } else if (!hash_table_delete_last_found(clause_table)) {
        snprintf(trusted_utils_msgstr, 512, "Clause deletion: Hash table error for ID %lu", id);
        return false;
    }
    return true;
}

void reset_assignments(void) {
    for (u64 i = 0; i < assigned_units->size; i++)
        var_values->data[assigned_units->data[i]] = 0;
    int_vec_clear(assigned_units);
}

bool check_clause(u64 base_id, const int* lits, int nb_lits, const u64* hints, int nb_hints) {

    int_vec_reserve(assigned_units, nb_lits + nb_hints);
    // Assume the negation of each literal in the new clause
    for (int i = 0; i < nb_lits; i++) {
        const int var = lits[i] > 0 ? lits[i] : -lits[i];
        var_values->data[var] = lits[i]>0 ? -1 : 1; // negated
        int_vec_push(assigned_units, var); // remember to reset later
    }

    // Traverse the provided hints to derive a conflict, i.e., the empty clause
    bool ok = true;
    for (int i = 0; i < nb_hints; i++) {

        // Find the clause for this hint
        const u64 hint_id = hints[i];
        const u8* cls = fetch_clause(hint_id);
        if (MALLOB_UNLIKELY(!cls)) {
            // ERROR - hint not found
            snprintf(trusted_utils_msgstr, 512, "Derivation %lu: hint %lu not found", base_id, hint_id);
            break;
        }

        // Interpret hint clause (should derive a new unit clause)
        int new_unit = 0;
        struct cclause_view view = get_cclause_view(&cls);
        int lit;
        while (cc_get_next_decompressed_lit(&view, &lit)) { // for each literal
            const int var = lit > 0 ? lit : -lit;
            if (var_values->data[var] == 0) {
                // Literal is unassigned
                if (MALLOB_UNLIKELY(new_unit != 0)) {
                    // ERROR - multiple unassigned literals in hint clause!
                    snprintf(trusted_utils_msgstr, 512, "Derivation %lu: multiple literals unassigned", base_id);
                    ok = false; break;
                }
                new_unit = lit;
                continue;
            }
            // Literal is fixed
            const bool sign = var_values->data[var]>0;
            if (MALLOB_UNLIKELY(sign == (lit>0))) {
                // ERROR - clause is satisfied, so it is not a correct hint
                snprintf(trusted_utils_msgstr, 512, "Derivation %lu: dependency %lu is satisfied", base_id, hint_id);
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
                break;
            }
            // Final hint produced empty clause - everything OK!
            reset_assignments();
            return true;
        }
        // Insert the new derived unit clause
        int var = new_unit > 0 ? new_unit : -new_unit;
        var_values->data[var] = new_unit>0 ? 1 : -1;
        int_vec_push(assigned_units, var); // remember to reset later
    }

    // ERROR - something went wrong
    if (trusted_utils_msgstr[0] == '\0')
        snprintf(trusted_utils_msgstr, 512, "Derivation %lu: no empty clause was produced", base_id);
    reset_assignments();
    return false;
}

// Quadratic check for clause equivalence - 
// assuming that most imported clauses are rather short.
bool clauses_equivalent(int* left_cls, int* right_cls) {
    int lit_idx = 0;
    for (; left_cls[lit_idx] != 0; lit_idx++) {
        const int left_lit = left_cls[lit_idx];
        bool found = false;
        for (int right_lit_idx = 0; right_cls[right_lit_idx] != 0; right_lit_idx++) {
            if (right_cls[right_lit_idx] == left_lit) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    const int left_size = lit_idx;
    for (lit_idx = 0; right_cls[lit_idx] != 0; lit_idx++) {}
    const int right_size = lit_idx;
    return left_size == right_size;
}
// Linear pass over compressed clause bytes, since they are "normalized" by compression
bool cclauses_equivalent(unsigned char* left_cls, unsigned char* right_cls) {
    int idx = 0;
    while (true) {
        if (left_cls[idx] == 0) return right_cls[idx] == 0;
        if (right_cls[idx] == 0) return false;
        if (left_cls[idx] != right_cls[idx]) return false;
        idx++;
    }
    return true;
}

bool lrat_check_add_axiomatic_clause(u64 id, int* lits, int nb_lits) {
    unsigned char* cls = cclause_init(lits, nb_lits);
    bool ok = true;
    if (done_loading) ok = hash_table_insert(clause_table, id, cls);
    else {
        u64_vec_push(input_clauses, (u64) cls);
        assert(id == input_clauses->size);
    }
    if (!ok) {
        if (lenient) {
            // In lenient mode, ignore the addition if and only if the clauses
            // are syntactically equivalent (except for literal ordering).
            unsigned char* old_cls = fetch_clause(id);
            if (old_cls && cclauses_equivalent(old_cls, cls)) {
                ok = true;
            }
        }
        if (!ok) snprintf(trusted_utils_msgstr, 512, "Insertion of clause %lu unsuccessful - already present?", id);
    }
    else if (nb_lits == 0) unsat_proven = true; // added top-level empty clause!
    return ok;
}

void lrat_check_init(int nb_vars, bool opt_check_model, bool opt_lenient) {
    clause_table = hash_table_init(14);
    input_clauses = u64_vec_init(1024);
    clause_to_add = int_vec_init(512);
    var_values = i8_vec_init(nb_vars+1);
    assigned_units = int_vec_init(512);
    check_model = opt_check_model;
    lenient = opt_lenient;
}

bool lrat_check_load(int lit) {
    if (lit == 0) {
        int_vec_push(clause_to_add, 0);
        siphash_update((u8*) clause_to_add->data, clause_to_add->size*sizeof(int));
        if (!lrat_check_add_axiomatic_clause(id_to_add, clause_to_add->data, clause_to_add->size - 1)) {
            return false;
        }
        id_to_add++;
        int_vec_clear(clause_to_add);
        return true;
    }
    int_vec_push(clause_to_add, lit);
    return true;
}

bool lrat_check_end_load(u8** out_sig) {
    if (clause_to_add->size > 0) {
        snprintf(trusted_utils_msgstr, 512, "literals left in unterminated clause");
        return false;
    }
    siphash_pad(2); // two-byte padding for formula signature input
    *out_sig = siphash_digest();
    done_loading = true;
    nb_loaded_clauses = id_to_add-1;
    u64_vec_shrink_to_fit(input_clauses);
    return true;
}


bool lrat_check_add_clause(u64 id, int* lits, int nb_lits, const u64* hints, int nb_hints) {
    if (!check_clause(id, lits, nb_lits, hints, nb_hints)) {
        return false;
    }
    return lrat_check_add_axiomatic_clause(id, lits, nb_lits);
}

bool lrat_check_delete_clause(const u64* ids, int nb_ids) {
    for (int i = 0; i < nb_ids; i++) {
        u64 id = ids[i];
        u8* cls = fetch_clause(id);
        if (!cls) {
            snprintf(trusted_utils_msgstr, 512, "Clause deletion: ID %lu not found", id);
            return false;
        }
        if (!free_clause(id, cls)) return false;
    }
    return true;
}

bool lrat_check_validate_unsat(void) {
    if (!done_loading) {
        snprintf(trusted_utils_msgstr, 512, "UNSAT validation illegal - loading formula was not concluded");
        return false;
    }
    if (!unsat_proven) {
        snprintf(trusted_utils_msgstr, 512, "UNSAT validation unsuccessful - did not derive or import empty clause");
        return false;
    }
    return true;
}

bool lrat_check_validate_sat(int* model, u64 size) {

    // Still loading the formula?
    if (!done_loading) {
        snprintf(trusted_utils_msgstr, 512, "SAT validation illegal - loading formula was not concluded");
        return false;
    }
    // Not executed with checking of models enabled?
    if (!check_model) {
        snprintf(trusted_utils_msgstr, 512, "SAT validation illegal - not executed to explicitly support this");
        return false;
    }
    // Check each original problem clause
    for (u64 id = 1; id <= nb_loaded_clauses; id++) {
        const unsigned char* cls = fetch_clause(id);
        if (MALLOB_UNLIKELY(!cls)) {
            // ERROR - clause not found
            snprintf(trusted_utils_msgstr, 512, "SAT validation: original ID %lu not found", id);
            return false;
        }
        // Iterate over the literals of the clause
        bool satisfied = false;
        struct cclause_view view = get_cclause_view(&cls);
        int lit;
        while (cc_get_next_decompressed_lit(&view, &lit)) {
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
