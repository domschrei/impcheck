
#include <stdbool.h>        // for bool, false, true
#include <stdio.h>          // for snprintf

#include "lrat_check.h"     // for lrat_check_add_axiomatic_clause, lrat_che...
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

struct sig_vec* signatures;

void compute_clause_signature(u64 id, const int* lits, int nb_lits, u32 rev, u8* out) {
    siphash_reset();
    siphash_update((u8*) &id, sizeof(u64));
    siphash_update((u8*) lits, nb_lits*sizeof(int));
    if (MALLOB_UNLIKELY(rev >= signatures->size)) {
        snprintf(trusted_utils_msgstr, 512, "No valid formula signature for rev. %i", rev);
        valid = false;
    } else {
        siphash_update((u8*) &signatures->data[rev], SIG_SIZE_BYTES);
    }
    const u8* hash_out = siphash_digest();
    trusted_utils_copy_bytes(out, hash_out, SIG_SIZE_BYTES);
}


void top_check_init(bool check_model, bool lenient) {
    lrat_check_init(check_model, lenient);
    signatures = sig_vec_init(1);
}

void top_check_commit_formula_sig(const u8* f_sig) {
    // Store formula signature to validate later after loading
    trusted_utils_copy_bytes((u8*) &formula_signature, f_sig, SIG_SIZE_BYTES);
    sig_vec_push(signatures, formula_signature);
    lrat_check_begin_load();
}

void top_check_load(int lit) {
    valid &= lrat_check_load(lit);
}

bool top_check_end_load(int* assumptions, int size) {
    u8* sig_from_chk;
    valid = valid && lrat_check_end_load(&sig_from_chk, assumptions, size);
    if (!valid) return false;
    // Check against provided signature
    valid = trusted_utils_equal_signatures(sig_from_chk, (u8*) &formula_signature);
    if (!valid) snprintf(trusted_utils_msgstr, 512, "Formula signature check failed");
    return valid;
}

bool top_check_produce(unsigned long id, int* literals, int nb_literals,
    const unsigned long* hints, int nb_hints, u8* out_sig_or_null) {
    
    // compute signature if desired
    if (out_sig_or_null) {
        compute_clause_signature(id, literals, nb_literals, signatures->size-1, out_sig_or_null);
    }
    // forward clause to checker
    valid &= lrat_check_add_clause(id, literals, nb_literals, hints, nb_hints);
    if (!valid) return false;
    return true;
}

bool top_check_import(unsigned long id, int* literals, int nb_literals,
    const u8* signature_data, int rev) {
    
    // verify signature
    SIG_TYPE computed_sig;
    compute_clause_signature(id, literals, nb_literals, rev, (u8*) &computed_sig);
    if (!trusted_utils_equal_signatures(signature_data, (u8*) &computed_sig)) {
        valid = false;
        snprintf(trusted_utils_msgstr, 512, "Signature check of clause %lu failed", id);
        return false;
    }

    // signature verified - forward clause to checker as an axiom
    valid &= lrat_check_add_axiomatic_clause(id, literals, nb_literals);
    return valid;
}

bool top_check_delete(const unsigned long* ids, int nb_ids) {
    return lrat_check_delete_clause(ids, nb_ids);
}

bool top_check_validate_unsat(u64 id, int* failed, int size, u8* out_signature_or_null) {
    valid &= lrat_check_validate_unsat(id, failed, size);
    if (!valid) {
        trusted_utils_log_err(trusted_utils_msgstr);
        return false;
    }
    if (out_signature_or_null)
        confirm_result((u8*) &formula_signature, 20, size, failed, out_signature_or_null);
    return true;
}

bool top_check_validate_sat(int* model, u64 size, u8* out_signature_or_null) {
    valid &= lrat_check_validate_sat(model, size);
    if (!valid) {
        trusted_utils_log_err(trusted_utils_msgstr);
        return false;
    }
    if (out_signature_or_null)
        confirm_result((u8*) &formula_signature, 10, 0, 0, out_signature_or_null);
    return true;
}

bool top_check_valid(void) {return valid;}
