
#include <stdbool.h>        // for bool, true, false
#include <stdio.h>          // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>         // for free
#include <time.h>           // for clock, CLOCKS_PER_SEC, clock_t
#include "top_check.h"      // for top_check_commit_formula_sig, top_check_d...
#include "trusted_utils.h"  // for trusted_utils_read_int, trusted_utils_log...

#define TYPE int
#define TYPED(THING) int_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

#define TYPE u64
#define TYPED(THING) u64_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

// Initialize and begin the loading stage.
// IN: #vars (int); 128-bit signature of the formula (from trusted parser)
// OUT: OK
#define TRUSTED_CHK_INIT 'B'

// Load a chunk of the original problem formula.
// IN: int k; sequence of k literals.
// OUT: (void)
#define TRUSTED_CHK_LOAD 'L'

// End the loading stage; verify the signature.
// IN: (void)
// OUT: OK
#define TRUSTED_CHK_END_LOAD 'E'

// Add the derivation of a new, local clause.
// IN: 64-bit ID; int k; sequence of k literals;
//     int l; sequence of l 64-bit hints;
//     "share" char (0|1) indicating whether to return a signature.
// OUT: OK; 128-bit signature only if "share"
#define TRUSTED_CHK_CLS_PRODUCE 'a'

// Import a clause from another solver.
// IN: 64-bit ID; int k; sequence of k literals; 128-bit signature.
// OUT: OK
#define TRUSTED_CHK_CLS_IMPORT 'i'

// Delete a sequence of clauses.
// IN: int k; sequence of k 64-bit IDs.
// OUT: OK
#define TRUSTED_CHK_CLS_DELETE 'd'

// Confirm that the formula is proven unsatisfiable.
// IN: (none)
// OUT: OK
#define TRUSTED_CHK_VALIDATE 'V'

// Terminate.
// IN: (none)
// OUT: OK
#define TRUSTED_CHK_TERMINATE 'T'

// Checker answer that everything is OK
#define TRUSTED_CHK_RES_ACCEPT 'A'
// Checker answer that an error occurred
#define TRUSTED_CHK_RES_ERROR 'E'

FILE* input; // named pipe
FILE* output; // named pipe
int nb_vars; // # variables in formula
signature formula_sig; // formula signature

bool do_logging = true;

// Buffering.
signature buf_sig;
struct int_vec* buf_lits;
struct u64_vec* buf_hints;


void say(bool ok) {
    trusted_utils_write_char(ok ? TRUSTED_CHK_RES_ACCEPT : TRUSTED_CHK_RES_ERROR, output);
}
void say_with_flush(bool ok) {
    say(ok);
    UNLOCKED_IO(fflush)(output);
}

u64 read_id() {
    return trusted_utils_read_ul(input);
}

void read_literals(int nb_lits) {
    int_vec_reserve(buf_lits, nb_lits);
    trusted_utils_read_ints(buf_lits->data, nb_lits, input);
}

void read_hints(int nb_hints) {
    u64_vec_reserve(buf_hints, nb_hints);
    trusted_utils_read_uls(buf_hints->data, nb_hints, input);
}

void init(const char* fifo_in, const char* fifo_out) {
    input = fopen(fifo_in, "r");
    if (!input) trusted_utils_exit_eof();
    output = fopen(fifo_out, "w");
    if (!output) trusted_utils_exit_eof();
    buf_lits = int_vec_init(1 << 14);
    buf_hints = u64_vec_init(1 << 14);
}

void end() {
    free(buf_hints);
    free(buf_lits);
    fclose(output);
    fclose(input);
}

int run() {
    clock_t start = clock();

    u64 nb_produced = 0;
    u64 nb_imported = 0;
    u64 nb_deleted = 0;

    bool reported_error = false;

    while (true) {
        int c = trusted_utils_read_char(input);
        if (c == TRUSTED_CHK_INIT) {

            nb_vars = trusted_utils_read_int(input);
            top_check_init(nb_vars);
            trusted_utils_read_sig(formula_sig, input);
            top_check_commit_formula_sig(formula_sig);
            say_with_flush(true);

        } else if (c == TRUSTED_CHK_LOAD) {

            const int nb_lits = trusted_utils_read_int(input);
            read_literals(nb_lits);
            for (int i = 0; i < nb_lits; i++) top_check_load(buf_lits->data[i]);
            // NO FEEDBACK

        } else if (c == TRUSTED_CHK_END_LOAD) {

            say_with_flush(top_check_end_load());

        } else if (c == TRUSTED_CHK_CLS_PRODUCE) {

            // parse
            const u64 id = read_id();
            const int nb_lits = trusted_utils_read_int(input);
            read_literals(nb_lits);
            const int nb_hints = trusted_utils_read_int(input);
            read_hints(nb_hints);
            const bool share = trusted_utils_read_bool(input);
            // forward to checker
            bool res = top_check_produce(id, buf_lits->data, nb_lits,
                buf_hints->data, nb_hints, share ? buf_sig : 0);
            // respond
            say(res);
            if (share) trusted_utils_write_sig(buf_sig, output);
            nb_produced++;

        } else if (c == TRUSTED_CHK_CLS_IMPORT) {

            // parse
            const u64 id = read_id();
            const int nb_lits = trusted_utils_read_int(input);
            read_literals(nb_lits);
            trusted_utils_read_sig(buf_sig, input);
            // forward to checker
            bool res = top_check_import(id, buf_lits->data, nb_lits, buf_sig);
            // respond
            say(res);
            nb_imported++;

        } else if (c == TRUSTED_CHK_CLS_DELETE) {
            
            // parse
            const int nb_hints = trusted_utils_read_int(input);
            read_hints(nb_hints);
            // forward to checker
            bool res = top_check_delete(buf_hints->data, nb_hints);
            // respond
            say(res);
            nb_deleted++;

        } else if (c == TRUSTED_CHK_VALIDATE) {

            bool res = top_check_validate(buf_sig);
            say(res);
            trusted_utils_write_sig(buf_sig, output);
            UNLOCKED_IO(fflush)(output);
            if (res) trusted_utils_log("UNSAT validated");

        } else if (c == TRUSTED_CHK_TERMINATE) {

            say_with_flush(true);
            break;

        } else {
            trusted_utils_log_err("Invalid directive!");
            break;
        }

        if (MALLOB_UNLIKELY(!top_check_valid())) {
            if (!reported_error) {
                trusted_utils_log_err(trusted_utils_errmsg);
                reported_error = true;
            }
        }
    }

    float elapsed = (float) (clock() - start) / CLOCKS_PER_SEC;

    char msg[128];
    snprintf(msg, 128, "cpu:%.3f prod:%lu imp:%lu del:%lu", elapsed, nb_produced, nb_imported, nb_deleted);
    trusted_utils_log(msg);

    return 0;
}
