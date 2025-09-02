
#pragma once

#include <stdbool.h>        // for bool
#include "trusted_utils.h"  // for u64, u8

void lrat_check_init(bool opt_check_model, bool opt_lenient);
bool lrat_check_begin_load(void);
bool lrat_check_load(int lit);
bool lrat_check_end_load(SIG_TYPE* out_sig, int* assumptions, int size);
bool lrat_check_add_axiomatic_clause(u64 id, int* lits, int nb_lits);
bool lrat_check_add_clause(u64 id, int* lits, int nb_lits, const u64* hints, int nb_hints);
bool lrat_check_delete_clause(const u64* ids, int nb_ids);
bool lrat_check_validate_unsat(u64 id, const int* failed, int size);
bool lrat_check_validate_sat(int* model, u64 size);
int lrat_check_get_nb_input_clauses(void);
