
#pragma once

#include <stdbool.h>        // for bool
#include "trusted_utils.h"  // for u64, u8

void checker_init(bool check_model, bool lenient);
void checker_commit_formula_sig(SIG_TYPE f_sig);
void checker_load(int lit);
bool checker_end_load(int* assumptions, int size);
bool checker_produce(unsigned long id, int* literals, int nb_literals,
    const unsigned long* hints, int nb_hints, SIG_TYPE* out_sig_or_null, u32* cidx_or_null);
bool checker_import(unsigned long id, int* literals, int nb_literals,
    SIG_TYPE signature_data, u32 cidx);
bool checker_delete(const unsigned long* ids, int nb_ids);
bool checker_validate_unsat(u64 id, int* failed, int size, SIG_TYPE* out_signature_or_null);
bool checker_validate_sat(int* model, u64 size, int* assumptions, u32 nb_assumptions, SIG_TYPE* out_signature_or_null);
bool checker_valid(void);
u32 checker_get_nb_input_clauses(void);
