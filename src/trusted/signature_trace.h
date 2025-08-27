
#pragma once

#include "trusted_utils.h"

struct sig_obligation {
    int res;
    SIG_TYPE sig_res;
    int nb_failed;
    int* failed_lits;
};

void signature_trace_init(const char* f);
bool signature_trace_get_next(struct sig_obligation** item);
void signature_trace_end(void);
