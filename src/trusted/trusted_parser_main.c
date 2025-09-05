
#include <stdint.h>
#include <stdio.h>

#include "parser_defs.h"
#include "secret.h"
#include "signature_trace.h"
#include "siphash.h"
#include "trusted_parser.h"
#include "trusted_utils.h"
#include "confirm.h"

FILE* f;
bool confirm;
FILE* tp_out;
FILE* inputlog_out;
int revision = -1;
struct siphash* siphash_parser;

void tp_init(const char* filename, FILE* out, bool confirm_results, FILE* inputlog) {
    f = fopen(filename, "r");
    tp_out = out;
    inputlog_out = inputlog;
    siphash_parser = siphash_init(SECRET_KEY);
    tp_inner_init(tp_out, siphash_parser);
    confirm = confirm_results;
}

FILE* tp_input_log(void) {
    return inputlog_out;
}

bool parse_increment(void) {
    revision++;

    // Read formula increment
    while (true) {
        int c_int = UNLOCKED_IO(fgetc)(f);
        if (tp_inner_process((char) c_int)) break;
    }
    if (!tp_inner_input_valid()) return false;

    // Output increment with fingerprint
    tp_inner_output();
    u32 nb_read_cls = tp_inner_nb_read_clauses();

    SIG_TYPE sig_for = siphash_end_branch(siphash_parser, 0);
    trusted_utils_write_sig((u8*) &sig_for, tp_out);
    trusted_utils_write_int(IMPCHECK_MARKER_ENDOFINCREMENT, tp_out);

    // If a result with signature is provided, confirm it
    if (confirm) {

        struct sig_obligation* item;
        if (!signature_trace_get_next(&item)) {
            trusted_utils_log_err("Missing or malformed signature obligation!");
            printf("s NOT VERIFIED\n");
            return false;
        }
        if (nb_read_cls != item->cidx) {
            snprintf(trusted_utils_msgstr, 512, "Unexpected clause index %u (read until index %u)!", item->cidx, nb_read_cls);
            trusted_utils_log_err(trusted_utils_msgstr);
            printf("s NOT VERIFIED\n");
            return false;
        }

        // recompute and validate report signature
        SIG_TYPE sig_res = confirm_result(sig_for, (u8) item->res, item->nb_lits, item->lits);
        if (!trusted_utils_equal_signatures(sig_res, item->sig_res)) {
            trusted_utils_log_err("Result signature does not match!");
            printf("s NOT VERIFIED\n");
            return false;
        }

        if (item->res == 10)
            printf("s VERIFIED SATISFIABLE cidx=%u rev=%i\n", item->cidx, revision);
        if (item->res == 20)
            printf("s VERIFIED UNSATISFIABLE cidx=%u rev=%i\n", item->cidx, revision);
    }
    return true;
}

bool tp_parse(void) {
    while (!tp_inner_input_finished()) {
        if (!parse_increment()) break;
    }
    return tp_inner_input_finished() && tp_inner_input_valid();
}

void tp_end(void) {
    tp_inner_end();
}
