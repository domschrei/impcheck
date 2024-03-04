
#pragma once

#include "trusted_utils.h"

void lrat_check_init(int nb_vars, const u8* sig_key_128bit);
bool lrat_check_load(int lit);
bool lrat_check_end_load(u8** out_sig);
bool lrat_check_add_axiomatic_clause(u64 id, const int* lits, int nb_lits);
bool lrat_check_add_clause(u64 id, const int* lits, int nb_lits, const u64* hints, int nb_hints);
bool lrat_check_delete_clause(const u64* ids, int nb_ids);
bool lrat_check_validate_unsat();
bool lrat_check_validate_sat(int* model, u64 size);
