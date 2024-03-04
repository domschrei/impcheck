
#include <stdbool.h>         // for bool
#include <stdio.h>           // for fopen, FILE
#include <stdlib.h>          // for abort
#include "trusted_parser.h"  // for tp_init, tp_parse
#include "trusted_utils.h"   // for trusted_utils_begins_with

int main(int argc, char *argv[]) {

    const char opt_formula_input[] = "-formula-input=";
    const char opt_parsed_formula[] = "-fifo-parsed-formula=";
    const char *formula_input = "", *fifo_parsed_formula = "";
    for (int i = 0; i < argc; i++) {
        if (trusted_utils_begins_with(argv[i], opt_formula_input))
            formula_input = argv[i] + (sizeof(opt_formula_input)-1);
        if (trusted_utils_begins_with(argv[i], opt_parsed_formula))
            fifo_parsed_formula = argv[i] + (sizeof(opt_parsed_formula)-1);
    }

    // Parse
    FILE* source = fopen(fifo_parsed_formula, "w");
    tp_init(formula_input, source);
    bool ok = tp_parse();
    if (!ok) abort();
    return 0;
}
