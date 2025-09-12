
#include <stdbool.h>         // for bool
#include <stdio.h>           // for fopen, FILE
#include "signature_trace.h"
#include "trusted_parser.h"  // for tp_init, tp_parse
#include "trusted_utils.h"   // for trusted_utils_begins_with
#include "keygen.h"

int error(void) {
    printf("s NOT VERIFIED\n");
    return 1;
}

int main(int argc, char *argv[]) {

    if (argc <= 1) {
        printf("Usage: %s -key-seed=<key-seed> -formula=<formula-file> -witness=<witness-file>\n", argv[0]);
        return 1;
    }

    const char *formula_input = "", *trace_input = "", *seed_str = "0";
    for (int i = 0; i < argc; i++) {
        trusted_utils_try_match_arg(argv[i], "-formula=", &formula_input);
        trusted_utils_try_match_arg(argv[i], "-witness=", &trace_input);
        trusted_utils_try_match_arg(argv[i], "-key-seed=", &seed_str);
    }
    generate_key(seed_str);

    // Initialize parser of signature trace file
    signature_trace_init(trace_input);

    // Parse formula and check signature obligations on the go
    FILE* sink = fopen("/dev/null", "w"); // write formula to /dev/null
    tp_init(formula_input, sink, true, 0);
    bool ok = tp_parse();
    if (!ok) {
        trusted_utils_log_err("Problem during parsing");
        return error();
    }

    signature_trace_end();
    return 0;
}
