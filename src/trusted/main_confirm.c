
#include <stdbool.h>         // for bool
#include <stdio.h>           // for fopen, FILE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
        printf("Usage: %s -key-seed=<key-seed> -formula=<formula-file> "
            "( -witness=<witness-file> | -result=<result-code> -sig=<result-fingerprint> "
            "[-cidx=<read-clauses> -assumptions='<a1 a2 ...>'] )\n", argv[0]);
        return 1;
    }

    const char *formula_input = "", *trace_input = "", *seed_str = "0";
    const char *res_str = "", *sig_str = "", *cidx_str = "", *asmpt_str = "";
    for (int i = 0; i < argc; i++) {
        trusted_utils_try_match_arg(argv[i], "-formula=", &formula_input);
        trusted_utils_try_match_arg(argv[i], "-key-seed=", &seed_str);

        trusted_utils_try_match_arg(argv[i], "-witness=", &trace_input);

        trusted_utils_try_match_arg(argv[i], "-result=", &res_str);
        trusted_utils_try_match_arg(argv[i], "-sig=", &sig_str);
        trusted_utils_try_match_arg(argv[i], "-cidx=", &cidx_str);
        trusted_utils_try_match_arg(argv[i], "-assumptions=", &asmpt_str);
    }
    generate_key(seed_str);

    bool create_trace = false;
    if (res_str[0] != '\0' && sig_str[0] != '\0') {
        trusted_utils_log("Running in direct check mode (-result and -sig provided)");
        create_trace = true;
    } else if (trace_input[0] != '\0') {
        trusted_utils_log("Running in incremental trace mode (-witness provided)");
    } else {
        trusted_utils_log_err("Provide either -result=<res> -sig=<fingerprint> or -witness=<trace-input>");
        return error();
    }

    char ftrace_path[64];
    if (create_trace) {
        int result = atoi(res_str);
        if (result != 10 && result != 20) {
            trusted_utils_log_err("Result code missing or invalid");
            return error();
        }
        if (strnlen(sig_str, 2*SIG_SIZE_BYTES+1) != 2*SIG_SIZE_BYTES) {
            trusted_utils_log_err("Result signature missing or malformed");
            return error();
        }
        int cidx = 0;
        if (cidx_str[0] != '\0') cidx = atoi(cidx_str);

        // Create trace file to check
        snprintf(ftrace_path, 63, "/tmp/impchk-trace.%i", getpid());
        FILE* ftrace = fopen(ftrace_path, "w");
        fprintf(ftrace, "%i %i %s", cidx, result, sig_str);
        if (asmpt_str[0] != '\0') fprintf(ftrace, " %s\n", asmpt_str);
        else fprintf(ftrace, "\n");
        fclose(ftrace);
    }

    signature_trace_init(create_trace ? ftrace_path : trace_input);

    // Parse formula and check signature obligations on the go
    FILE* sink = fopen("/dev/null", "w"); // write formula to /dev/null
    tp_init(formula_input, sink, true, create_trace, 0);
    bool ok = tp_parse();
    if (!ok) {
        trusted_utils_log_err("Problem during parsing");
        return error();
    }

    signature_trace_end();
    if (create_trace) remove(ftrace_path);

    return 0;
}
