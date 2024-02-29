
#include <stdbool.h>        // for bool, false, true
#include <stdio.h>          // for snprintf
#include "clause.h"         // for clause_free, clause_init
#include "hash.h"           // for hash_table_find, hash_table_delete_last_f...
#include "siphash.h"        // for siphash_digest, siphash_init, siphash_update
#include "trusted_utils.h"  // for u64, trusted_utils_errmsg, MALLOB_UNLIKELY

#define TYPE int
#define TYPED(THING) int_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

#define TYPE u64
#define TYPED(THING) u64_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

#define TYPE signed char
#define TYPED(THING) i8_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

// The hash table where we keep all clauses and which uses most of our RAM.
// We still use a power-of-two growth policy since this makes lookups faster.
struct hash_table* clause_table;

struct i8_vec* var_values;
bool unsat_proven = false;

u64 id_to_add = 1;
struct int_vec* clause_to_add;
bool done_loading = false;

// Keep track of asserted unit clauses in a stack
struct int_vec* assigned_units;



bool check_clause(u64 base_id, const int* lits, int nb_lits, const u64* hints, int nb_hints) {

    int_vec_reserve(assigned_units, nb_lits + nb_hints);
    // Assume the negation of each literal in the new clause
    for (int i = 0; i < nb_lits; i++) {
        int var = lits[i] > 0 ? lits[i] : -lits[i];
        var_values->data[var] = lits[i]>0 ? -1 : 1; // negated
        int_vec_push(assigned_units, var); // remember to reset later
        //printf("%i ", lits[i]);
    }
    //printf("0 ");

    // Traverse the provided hints to derive a conflict, i.e., the empty clause
    bool ok = true;
    for (int i = 0; i < nb_hints; i++) {

        // Find the clause for this hint
        u64 hint_id = hints[i];
        //printf("%lu ( ", hint_id);
        int* cls = (int*) hash_table_find(clause_table, hint_id);
        if (MALLOB_UNLIKELY(!cls)) {
            // ERROR - hint not found
            snprintf(trusted_utils_errmsg, 512, "Derivation %lu: hint %lu not found", base_id, hint_id);
            break;
        }

        // Interpret hint clause (should derive a new unit clause)
        int new_unit = 0;
        for (int lit_idx = 0; ; lit_idx++) { // for each literal ...
            int lit = cls[lit_idx];
            //printf("%i ", lit);
            if (lit == 0) break;           // ... until termination zero
            int var = lit > 0 ? lit : -lit;
            if (var_values->data[var] == 0) {
                // Literal is unassigned
                if (MALLOB_UNLIKELY(new_unit != 0)) {
                    // ERROR - multiple unassigned literals in hint clause!
                    snprintf(trusted_utils_errmsg, 512, "Derivation %lu: multiple literals unassigned", base_id);
                    ok = false; break;
                }
                new_unit = lit;
                continue;
            }
            // Literal is fixed
            bool sign = var_values->data[var]>0;
            if (MALLOB_UNLIKELY(sign == (lit>0))) {
                // ERROR - clause is satisfied, so it is not a correct hint
                snprintf(trusted_utils_errmsg, 512, "Derivation %lu: dependency %lu is satisfied", base_id, hint_id);
                ok = false; break;
            }
            // All OK - literal is false, thus (virtually) removed from the clause
        }
        //printf(") ");
        if (!ok) break; // error detected - stop

        // NO unit derived?
        if (new_unit == 0) {
            // No unassigned literal in the clause && clause not satisfied
            // -> Empty clause derived.
            if (MALLOB_UNLIKELY(i+1 < nb_hints)) {
                // ERROR - not at the final hint yet!
                snprintf(trusted_utils_errmsg, 512, "Derivation %lu: empty clause produced at non-final hint %lu", base_id, hint_id);
                break;
            }
            // Final hint produced empty clause - everything OK!
            for (u64 i = 0; i < assigned_units->size; i++)
                var_values->data[assigned_units->data[i]] = 0; // reset variable values
            int_vec_clear(assigned_units);
            //printf("\n");
            return true;
        }
        // Insert the new derived unit clause
        int var = new_unit > 0 ? new_unit : -new_unit;
        var_values->data[var] = new_unit>0 ? 1 : -1;
        int_vec_push(assigned_units, var); // remember to reset later
    }

    // ERROR - something went wrong
    if (trusted_utils_errmsg[0] == '\0')
        snprintf(trusted_utils_errmsg, 512, "Derivation %lu: no empty clause was produced", base_id);
    for (u64 i = 0; i < assigned_units->size; i++)
        var_values->data[assigned_units->data[i]] = 0; // reset variable values
    int_vec_clear(assigned_units);
    return false;
}






bool lrat_check_add_axiomatic_clause(u64 id, const int* lits, int nb_lits) {
    int* cls = clause_init(lits, nb_lits);
    bool ok = hash_table_insert(clause_table, id, cls);
    if (!ok) snprintf(trusted_utils_errmsg, 512, "Insertion of clause %lu unsuccessful - already present?", id);
    else if (nb_lits == 0) unsat_proven = true; // added top-level empty clause!
    return ok;
}

void lrat_check_init(int nb_vars, const u8* sig_key_128bit) {
    clause_table = hash_table_init(16);
    clause_to_add = int_vec_init(512);
    var_values = i8_vec_init(nb_vars+1);
    assigned_units = int_vec_init(512);
    siphash_init(sig_key_128bit);
}

bool lrat_check_load(int lit) {
    if (lit == 0) {
        if (!lrat_check_add_axiomatic_clause(id_to_add, clause_to_add->data, clause_to_add->size)) {
            return false;
        }
        id_to_add++;
        int_vec_push(clause_to_add, 0);
        siphash_update((u8*) clause_to_add->data, clause_to_add->size*sizeof(int));
        int_vec_clear(clause_to_add);
        return true;
    }
    int_vec_push(clause_to_add, lit);
    return true;
}

bool lrat_check_end_load(u8** out_sig) {
    if (clause_to_add->size > 0) {
        snprintf(trusted_utils_errmsg, 512, "literals left in unterminated clause");
        return false;
    }
    *out_sig = siphash_digest();
    done_loading = true;
    return true;
}


bool lrat_check_add_clause(u64 id, const int* lits, int nb_lits, const u64* hints, int nb_hints) {
    if (!check_clause(id, lits, nb_lits, hints, nb_hints)) {
        return false;
    }
    return lrat_check_add_axiomatic_clause(id, lits, nb_lits);
}

bool lrat_check_delete_clause(const u64* ids, int nb_ids) {
    for (int i = 0; i < nb_ids; i++) {
        u64 id = ids[i];
        int* cls = hash_table_find(clause_table, id);
        if (!cls) {
            snprintf(trusted_utils_errmsg, 512, "Clause deletion: ID %lu not found", id);
            return false;
        }
        clause_free(cls);
        if (!hash_table_delete_last_found(clause_table)) {
            snprintf(trusted_utils_errmsg, 512, "Clause deletion: Hash table error for ID %lu", id);
            return false;
        }
    }
    return true;
}

bool lrat_check_validate_unsat() {
    if (!done_loading) {
        snprintf(trusted_utils_errmsg, 512, "UNSAT validation illegal - loading formula was not concluded");
        return false;
    }
    if (!unsat_proven) {
        snprintf(trusted_utils_errmsg, 512, "UNSAT validation unsuccessful - did not derive or import empty clause");
        return false;
    }
    return true;
}
