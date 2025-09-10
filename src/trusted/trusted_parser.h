
#pragma once

#include <stdbool.h>        // for bool
#include <stdio.h>          // for FILE

void tp_init(const char* filename, FILE* out, bool confirm_results, FILE* inputlog);
bool tp_parse(void);
void tp_end(void);
