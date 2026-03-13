
#include <stdbool.h>         // for bool
#include <stdio.h>           // for fopen, FILE
#include <stdlib.h>          // for abort
#include "trusted_parser.h"  // for tp_init, tp_parse
#include "trusted_utils.h"   // for trusted_utils_begins_with
#include "keygen.h"

int main(int argc, char *argv[]) {

    if (argc <= 1) {
        printf("Usage: %s -key-seed=<key-seed> -formula=<input-file> -output=<output-file>"
            " [-input-log=<log-file-for-input>]\n", argv[0]);
        return 1;
    }

    const char *formula_input = "", *fifo_parsed_formula = "", *seed_str = "0", *inputlog = "";
    for (int i = 0; i < argc; i++) {
        trusted_utils_try_match_arg(argv[i], "-formula=", &formula_input);
        trusted_utils_try_match_arg(argv[i], "-output=", &fifo_parsed_formula);
        trusted_utils_try_match_arg(argv[i], "-key-seed=", &seed_str);
        trusted_utils_try_match_arg(argv[i], "-input-log=", &inputlog);
    }
    generate_key(seed_str);

    // Parse
    FILE* source = fopen(fifo_parsed_formula, "w");
    FILE* f_inputlog = 0;
    if (inputlog) f_inputlog = fopen(inputlog, "w");
    tp_init(formula_input, source, false, false, f_inputlog);
    bool ok = tp_parse();
    if (!ok) abort();
    return 0;
}
