
#include "signature_trace.h"
#include "trusted_utils.h"
#include "assert.h"

// Instantiate int_vec
#define TYPE int
#define TYPED(THING) int_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

FILE* sigtrace_in;
struct int_vec* failed_lits;
struct sig_obligation item_out;

int parse_number_and_endchar(char* endchar) {
    int res = 0;
    int sign = 1;
    while (true) {
        char c = trusted_utils_read_char(sigtrace_in);
        if (c == '-' && res == 0) {
            sign *= -1;
            continue;
        }
        if (c < '0' || c > '9') {
            *endchar = c;
            break;
        }
        res = 10*res + (c-'0');
    }
    return sign * res;
}

void signature_trace_init(const char* path) {
    sigtrace_in = fopen(path, "r");
    assert(sigtrace_in);
    failed_lits = int_vec_init(64);
}

bool signature_trace_get_next(struct sig_obligation** ptr_out) {
    *ptr_out = &item_out;
    if (!sigtrace_in) return false;

    char sig_str[2*SIG_SIZE_BYTES+1];
    char c = '\0';
    int_vec_clear(failed_lits);
    
    // result (+space)
    item_out.res = parse_number_and_endchar(&c);
    if (c != ' ') return false;

    // result signature
    trusted_utils_read_objs(sig_str, 1, 2*SIG_SIZE_BYTES, sigtrace_in);
    sig_str[2*SIG_SIZE_BYTES] = '\0';
    trusted_utils_str_to_sig((const char*) sig_str, (u8*) &item_out.sig_res);

    // whitespace or linebreak
    c = trusted_utils_read_char(sigtrace_in);

    // failed literals
    item_out.nb_failed = 0;
    while (c == ' ') {
        int lit = parse_number_and_endchar(&c);
        int_vec_push(failed_lits, lit);
        item_out.nb_failed++;
    }
    item_out.failed_lits = failed_lits->data;

    // end: linebreak
    return c == '\n';
}

void signature_trace_end(void) {
    fclose(sigtrace_in);
}
