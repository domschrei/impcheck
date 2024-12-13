
#pragma once

#include <stdbool.h>

void tc_init(const char* fifo_in, const char* fifo_out, unsigned long num_solvers);
void tc_end();
int tc_run(bool check_model, bool lenient);
