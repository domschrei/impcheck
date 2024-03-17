
#include <stdio.h>
#include <unistd.h>

#include "trusted_utils.h"
#include "trusted_checker.h"
#if IMPCHECK_WRITE_DIRECTIVES
#include "../writer.h"
#endif

int main(int argc, char *argv[]) {

    const char opt_directives[] = "-fifo-directives=";
    const char opt_feedback[] = "-fifo-feedback=";
    const char opt_checkmodel[] = "-check-model";
    const char *fifo_directives = "", *fifo_feedback = "";
    bool check_model = false;
    for (int i = 1; i < argc; i++) {
        if (trusted_utils_begins_with(argv[i], opt_directives))
            fifo_directives = argv[i] + (sizeof(opt_directives)-1);
        if (trusted_utils_begins_with(argv[i], opt_feedback))
            fifo_feedback = argv[i] + (sizeof(opt_feedback)-1);
        if (trusted_utils_begins_with(argv[i], opt_checkmodel))
            check_model = true;
    }

#if IMPCHECK_WRITE_DIRECTIVES
    char output_path[512];
    snprintf(output_path, 512, "directives.%i.impcheck", getpid());
    writer_init(output_path);
#endif

    tc_init(fifo_directives, fifo_feedback);
    int res = tc_run(check_model);
    fflush(stdout);
    return res;
}
