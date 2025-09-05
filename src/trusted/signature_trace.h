
#pragma once

#include "trusted_utils.h"

struct sig_obligation {
    u32 cidx;
    u32 res;
    SIG_TYPE sig_res;
    u32 nb_lits;
    int* lits;
};

void signature_trace_init(const char* f);
bool signature_trace_get_next(struct sig_obligation** item);
void signature_trace_end(void);
