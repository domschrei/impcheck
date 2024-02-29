
#include <stdio.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "trusted_utils.h"
#include "trusted_checker.h"
#if WRITE_DIRECTIVES
#include "../writer.h"
#endif

int main(int argc, char *argv[]) {

    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);

    const char opt_directives[] = "-fifo-directives=";
    const char opt_feedback[] = "-fifo-feedback=";
    const char *fifo_directives = "", *fifo_feedback = "";
    for (int i = 0; i < argc; i++) {
        if (trusted_utils_begins_with(argv[i], opt_directives))
            fifo_directives = argv[i] + (sizeof(opt_directives)-1);
        if (trusted_utils_begins_with(argv[i], opt_feedback))
            fifo_feedback = argv[i] + (sizeof(opt_feedback)-1);
    }

#if WRITE_DIRECTIVES
    char output_path[512];
    snprintf(output_path, 512, "directives.%i.parlrat", getpid());
    writer_init(output_path);
#endif

    init(fifo_directives, fifo_feedback);
    int res = run();
    fflush(stdout);
    return res;
}
