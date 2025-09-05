
#pragma once

#include "siphash.h"
#include "trusted_utils.h"
#include <stdbool.h>        // for bool
#include <stdio.h>          // for FILE

void tp_init(const char* filename, FILE* out, bool confirm_results, FILE* inputlog);
bool tp_parse(void);
void tp_end(void);
FILE* tp_input_log(void);

void tp_inner_init(FILE* out, struct siphash* sh);
bool tp_inner_process(char c);
bool tp_inner_input_valid(void);
bool tp_inner_input_finished(void);
u32 tp_inner_nb_read_clauses(void);
void tp_inner_output(void);
void tp_inner_end(void);
