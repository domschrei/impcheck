
#include <stdbool.h>        // for bool, false, true
#include <stdio.h>          // for snprintf
#include <stdlib.h>
#include <unistd.h>

#include "hash.h"
#include "lrat_check.h"     // for lrat_check_add_axiomatic_clause, lrat_che...
#include "secret.h"
#include "siphash.h"        // for siphash_update, siphash_digest, siphash_r...
#include "trusted_utils.h"  // for u8, trusted_utils_copy_bytes, trusted_uti...
#include "confirm.h"

bool parsed_formula = false;
SIG_TYPE formula_signature;

bool valid = true;

// Instantiate sig_vec
#define TYPE SIG_TYPE
#define TYPED(THING) sig_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

//struct sig_vec* signatures;
struct hash_table* ht_signatures;

struct siphash* siphash_cls;

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

void top_check_init(bool check_model, bool lenient) {
    lrat_check_init(check_model, lenient);
    //signatures = sig_vec_init(1);
    ht_signatures = hash_table_init(6);
    siphash_cls = siphash_init(SECRET_KEY);
}

void top_check_commit_formula_sig(SIG_TYPE f_sig) {
    if (!valid) return;
    // Store formula signature to validate later after loading
    formula_signature = f_sig;
    //sig_vec_push(signatures, formula_signature);
    valid = valid && lrat_check_begin_load();
}

void top_check_load(int lit) {
    valid = valid && lrat_check_load(lit);
}

bool top_check_end_load(int* assumptions, int size) {
    if (!valid) return false;
    SIG_TYPE sig_from_chk;
    valid = lrat_check_end_load(&sig_from_chk, assumptions, size);
    if (!valid) return false;
    // Check against provided signature
    valid = trusted_utils_equal_signatures(sig_from_chk, formula_signature);
    if (!valid) {
        snprintf(trusted_utils_msgstr, 512, "Formula signature check failed");
        return false;
    }
    // Insert this signature in the hash table of formula signatures
    u8* newsig = trusted_utils_malloc(sizeof(SIG_TYPE));
    trusted_utils_copy_bytes(newsig, (u8*) &formula_signature, SIG_SIZE_BYTES);
    hash_table_insert(ht_signatures, lrat_check_get_nb_input_clauses(), newsig);
    return valid;
}

bool top_check_produce(unsigned long id, int* literals, int nb_literals,
    const unsigned long* hints, int nb_hints, SIG_TYPE* out_sig_or_null, u32* cidx_or_null) {

    // compute signature if desired
    if (out_sig_or_null) {
        *out_sig_or_null = compute_clause_signature(id, literals, nb_literals, lrat_check_get_nb_input_clauses());
    }
    // forward clause to checker
    valid = valid && lrat_check_add_clause(id, literals, nb_literals, hints, nb_hints);
    if (valid && cidx_or_null) *cidx_or_null = lrat_check_get_nb_input_clauses();
    return valid;
}

bool top_check_import(unsigned long id, int* literals, int nb_literals,
    SIG_TYPE signature_data, u32 cidx) {
    
    // verify signature
    SIG_TYPE computed_sig = compute_clause_signature(id, literals, nb_literals, cidx);
    if (!valid) return false;
    if (!trusted_utils_equal_signatures(signature_data, computed_sig)) {
        valid = false;
        snprintf(trusted_utils_msgstr, 512, "Signature check of clause %lu from cidx %i failed", id, cidx);
        return false;
    }

    // signature verified - forward clause to checker as an axiom
    valid = valid && lrat_check_add_axiomatic_clause(id, literals, nb_literals);
    return valid;
}

bool top_check_delete(const unsigned long* ids, int nb_ids) {
    return lrat_check_delete_clause(ids, nb_ids);
}

bool top_check_validate_unsat(u64 id, int* failed, int size, SIG_TYPE* out_signature_or_null) {
    valid = valid && lrat_check_validate_unsat(id, failed, size);
    if (!valid) {
        trusted_utils_log_err(trusted_utils_msgstr);
        return false;
    }
    if (out_signature_or_null)
        *out_signature_or_null = confirm_result(formula_signature, 20, size, failed);
    return true;
}

bool top_check_validate_sat(int* model, u64 size, SIG_TYPE* out_signature_or_null) {
    valid = valid && lrat_check_validate_sat(model, size);
    if (!valid) {
        trusted_utils_log_err(trusted_utils_msgstr);
        return false;
    }
    if (out_signature_or_null)
        *out_signature_or_null = confirm_result(formula_signature, 10, 0, 0);
    return true;
}

bool top_check_valid(void) {return valid;}
