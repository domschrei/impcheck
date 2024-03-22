
#include <stdio.h>
#include <unistd.h>

#include "trusted_utils.h"
#include "trusted_checker.h"
#if IMPCHECK_WRITE_DIRECTIVES
#include "../writer.h"
#endif

int main(int argc, char *argv[]) {

    const char *fifo_directives = "", *fifo_feedback = "";
    bool check_model = false;
    for (int i = 1; i < argc; i++) {
        trusted_utils_try_match_arg(argv[i], "-fifo-directives=", &fifo_directives);
        trusted_utils_try_match_arg(argv[i], "-fifo-feedback=", &fifo_feedback);
        trusted_utils_try_match_flag(argv[i], "-check-model", &check_model);
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
