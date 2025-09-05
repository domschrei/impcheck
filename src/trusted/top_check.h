
#include <stdbool.h>        // for bool
#include "trusted_utils.h"  // for u8, u64

// Top level checking procedure. Checks clauses, validates signatures,
// and returns certificates for (un)satisfiability.

void top_check_init(bool check_model, bool lenient);
void top_check_commit_formula_sig(SIG_TYPE f_sig);
void top_check_load(int lit);
bool top_check_end_load(int* assumptions, int size);
bool top_check_produce(unsigned long id, int* literals, int nb_literals,
    const unsigned long* hints, int nb_hints, SIG_TYPE* out_sig_or_null, u32* cidx_or_null);
bool top_check_import(unsigned long id, int* literals, int nb_literals,
    SIG_TYPE signature_data, u32 cidx);
bool top_check_delete(const unsigned long* ids, int nb_ids);
bool top_check_validate_unsat(u64 id, int* failed, int size, SIG_TYPE* out_signature_or_null);
bool top_check_validate_sat(int* model, u64 size, int* assumptions, u32 nb_assumptions, SIG_TYPE* out_signature_or_null);
bool top_check_valid(void);
