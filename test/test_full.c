
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

// contains the definitions of constants for our parser + checker interface
#include "../src/trusted/checker_interface.h"
#include "../src/trusted/parser_defs.h"

// other imports from this project - just for convenience, not strictly needed
#include "test.h"
#include "../src/trusted/trusted_utils.h"
// Instantiate int_vec (poor man's template programming in C)
#define TYPE int
#define TYPED(THING) int_ ## THING
#include "../src/trusted/vec.h"
#undef TYPED
#undef TYPE



// We only use this to create unique names for the named pipes
// for cases where multiple checkers|parsers run at once.
u64 checker_instance_id = 1;

// Fork the process into a parent and a child.
bool do_fork(void) {
    pid_t child_pid = fork();
    do_assert(child_pid >= 0);
    return child_pid == 0;
}

// Set up a "named pipe" special file at the specified location.
void create_pipe(const char* path) {
    remove(path);
    int res = mkfifo(path, 0777);
    do_assert(res == 0);
}

// Flush the output (pipe), wait until an 'OK' arrives from the input (pipe)
void await_ok(FILE* out, FILE* in) {
    fflush(out);
    bool ok = trusted_utils_read_char(in);
    do_assert(ok);
}

void load_next_formula_increment(FILE* out_parsed, FILE* out_directives, FILE* in_feedback) {

    struct int_vec* fvec = int_vec_init(1<<14);
    struct int_vec* avec = int_vec_init(64);

    // const int nb_vars = trusted_utils_read_int(out_parsed);
    // /*const int nb_clauses = */trusted_utils_read_int(out_parsed);

    bool assumptions = false;
    while (true) {
        // (note: cannot use trusted_utils_read_int here since EOF makes it terminate)
        int lit;
        const int nb_read = fread(&lit, sizeof(int), 1, out_parsed);
        if (nb_read == 0) break;
        if (lit == IMPCHECK_MARKER_ENDOFINCREMENT) {
            break; // done reading this increment!
        }
        if (lit == IMPCHECK_MARKER_ASSUMPTIONS) {
            assumptions = true;
            continue;
        }
        if (assumptions && lit == 0) {
            assumptions = false;
            continue;
        }
        int_vec_push(assumptions ? avec : fvec, lit);
    }
    const int* f = fvec->data;
    // the last SIG_SIZE_BYTES bytes of the "formula" are actually its signature
    const u8* fsig = ((u8*) (fvec->data + fvec->size)) - SIG_SIZE_BYTES;
    const u64 fsize = fvec->size - (SIG_SIZE_BYTES / sizeof(int));

    // Directive "BEGIN"
    trusted_utils_write_char(TRUSTED_CHK_BEGIN_LOAD, out_directives);
    trusted_utils_write_sig(fsig, out_directives);
    await_ok(out_directives, in_feedback);

    // Directive "LOAD"
    trusted_utils_write_char(TRUSTED_CHK_LOAD, out_directives);
    trusted_utils_write_int(fsize, out_directives);
    trusted_utils_write_ints(f, fsize, out_directives);
    // (no feedback)

    // Directive "END_LOAD"
    trusted_utils_write_char(TRUSTED_CHK_END_LOAD, out_directives);
    trusted_utils_write_int(avec->size, out_directives); // # assumptions
    trusted_utils_write_ints(avec->data, avec->size, out_directives);
    await_ok(out_directives, in_feedback);

    int_vec_free(fvec);
    int_vec_free(avec);
}

// Set up a ready-to-go pair of trusted parser and checker processes.
// - create all named pipes needed for inter-process communication
// - run a trusted parser instance and read the parsed formula
//   together with its signature
// - launch a trusted checker instance and forward the formula
//   with its signature
// - return an ID which is needed for the corresponding clean_up() below
// - return the file handles for writing directives and for reading
//   feedback via the two out params
u64 setup(const char* cnfInput, FILE** f_parsed_out, FILE** f_directives_out, FILE** f_feedback_out) {

    char charbuf[1024]; // to construct some strings

    // create pipes (delete old ones, if still present)
    char pipeParsed[64], pipeDirectives[64], pipeFeedback[64];
    snprintf(pipeParsed, 64, ".parsed.%lu.pipe", checker_instance_id);
    snprintf(pipeDirectives, 64, ".directives.%lu.pipe", checker_instance_id);
    snprintf(pipeFeedback, 64, ".feedback.%lu.pipe", checker_instance_id);
    create_pipe(pipeParsed);
    create_pipe(pipeDirectives);
    create_pipe(pipeFeedback);

    remove("sigtrace.txt"); // clean up prior sigtrace file if needed

    // Fork off a parser process.
    if (do_fork()) {
        // child: parser process
        snprintf(charbuf, 1024, "build/impcheck_parse -formula-input=%s \
            -fifo-parsed-formula=%s -input-log=input.log",
            cnfInput, pipeParsed);
        int res = system(charbuf);
        do_assert(res == 0);
        exit(0); // child process done
    } // -- parent

    // Fork off a checker process.
    if (do_fork()) {
        // child: checker process
        snprintf(charbuf, 1024, "build/impcheck_check -fifo-directives=%s -fifo-feedback=%s -check-model",
            pipeDirectives, pipeFeedback);
        int res = system(charbuf);
        do_assert(res == 0);
        exit(0); // child process done
    } // -- parent

    // open communication channels with parser and checker process
    FILE* out_parsed = fopen(pipeParsed, "r");
    FILE* out_directives = fopen(pipeDirectives, "w");
    FILE* in_feedback = fopen(pipeFeedback, "r");

    load_next_formula_increment(out_parsed, out_directives, in_feedback);

    // The checker process is now in a state where it can take
    // individual derivations, imports, deletions and such.

    // return file handles
    *f_parsed_out = out_parsed;
    *f_directives_out = out_directives;
    *f_feedback_out = in_feedback;

    // increment our checker ID for the next call
    return checker_instance_id++;
}

void append_to_trace_file(int result, const u8* sig, int nb_failed, int* failed) {

    // Convert result signature to a string
    char sigstr[2*SIG_SIZE_BYTES+1];
    trusted_utils_sig_to_str(sig, sigstr);

    // Write signature trace file
    FILE* f_sigtrace = fopen("sigtrace.txt", "a");
    fprintf(f_sigtrace, "%i %s", result, sigstr);
    for (int i = 0; i < nb_failed; i++) {
        fprintf(f_sigtrace, " %i", failed[i]);
    }
    fprintf(f_sigtrace, "\n");
    fclose(f_sigtrace);
}

bool confirm(const char* cnfInput) {

    // Execute confirmer sub-process
    char charbuf[1024];
    snprintf(charbuf, 1024, "build/impcheck_confirm -formula-input=%s -signature-input=%s",
        cnfInput, "sigtrace.txt");
    const int res = system(charbuf);

    return res == 0;
}

// Clean up a checker process previously opened via "setup". The checker must already
// have received and responded to a TERMINATE directive beforehand.
void clean_up(u64 checker_id, FILE* out_parsed, FILE* out_directives, FILE* in_feedback) {

    // "TERMINATE" directive
    trusted_utils_write_char(TRUSTED_CHK_TERMINATE, out_directives);
    await_ok(out_directives, in_feedback);

    // close pipes
    fclose(out_parsed);
    fclose(out_directives);
    fclose(in_feedback);

    // remove pipe files
    char pipeParsed[64], pipeDirectives[64], pipeFeedback[64];
    snprintf(pipeParsed, 64, ".parsed.%lu.pipe", checker_id);
    snprintf(pipeDirectives, 64, ".directives.%lu.pipe", checker_id);
    snprintf(pipeFeedback, 64, ".feedback.%lu.pipe", checker_id);
    remove(pipeParsed);
    remove(pipeDirectives);
    remove(pipeFeedback);

    // "join" checker and parser processes
    // NOTE: This wait() does not necessarily match the intended process.
    // Since all resources are cleaned up before a sub-process' termination anyway,
    // all we really want is to call wait() as often as there are sub-processes.
    wait(0);
    wait(0);
}

// Helper method to add and check a single clause derivation.
void produce_cls(FILE* out_directives, FILE* in_feedback,
    u64 id, int clslen, const int* lits, int hintlen, const u64* hints, u8* sig_or_null, u32* cidx_or_null) {

    trusted_utils_write_char(TRUSTED_CHK_CLS_PRODUCE, out_directives); // PRODUCE ("add") directive
    trusted_utils_write_ul(id, out_directives); // clause ID
    trusted_utils_write_int(clslen, out_directives); // clause length
    trusted_utils_write_ints(lits, clslen, out_directives); // literals
    trusted_utils_write_int(hintlen, out_directives); // # hints
    trusted_utils_write_uls(hints, hintlen, out_directives); // hints
    trusted_utils_write_bool(sig_or_null!=0, out_directives); // share / produce signature?
    await_ok(out_directives, in_feedback);
    if (sig_or_null != 0) {
        trusted_utils_read_sig(sig_or_null, in_feedback);
    }
    if (cidx_or_null != 0) {
        *cidx_or_null = trusted_utils_read_uint(in_feedback);
    }
}

// Helper method to import a single clause.
void import_cls(FILE* out_directives, FILE* in_feedback,
    u64 id, int clslen, const int* lits, u8* signature, u32 cidx) {

    trusted_utils_write_char(TRUSTED_CHK_CLS_IMPORT, out_directives); // IMPORT directive
    trusted_utils_write_ul(id, out_directives); // clause ID
    trusted_utils_write_int(clslen, out_directives); // clause length
    trusted_utils_write_ints(lits, clslen, out_directives); // literals
    trusted_utils_write_sig(signature, out_directives); // signature
    trusted_utils_write_uint(cidx, out_directives); // incremental clause index
    await_ok(out_directives, in_feedback);
}

// Helper method to delete a set of clauses (specified by IDs).
void delete_cls(FILE* out_directives, FILE* in_feedback, const u64* ids, int nb_ids) {

    trusted_utils_write_char(TRUSTED_CHK_CLS_DELETE, out_directives);
    trusted_utils_write_int(nb_ids, out_directives);
    trusted_utils_write_uls(ids, nb_ids, out_directives);
    await_ok(out_directives, in_feedback);
}




/*
Full "trusted solving" run on a trivial satisfiable formula.
*/
void test_trivial_sat(void) {
    printf("[TEST] --- begin test_trivial_sat() ---\n");

    // Parse formula and set up trusted checker process
    const char* cnf = "cnf/trivial-sat.cnf";
    FILE *out_parsed, *out_directives, *in_feedback;
    u64 chkid = setup(cnf, &out_parsed, &out_directives, &in_feedback);

    // VALIDATE_SAT
    trusted_utils_write_char(TRUSTED_CHK_VALIDATE_SAT, out_directives);
    trusted_utils_write_int(2, out_directives);
    int model[2] = {1, -2};
    trusted_utils_write_ints(model, 2, out_directives);
    await_ok(out_directives, in_feedback);
    u8 sat_sig[SIG_SIZE_BYTES];
    trusted_utils_read_sig(sat_sig, in_feedback);

    // (optional) confirm with "confirmer" module
    append_to_trace_file(10, sat_sig, 0, 0);
    bool ok = confirm(cnf);
    do_assert(ok);

    // TERMINATE
    clean_up(chkid, out_parsed, out_directives, in_feedback);
    printf("[TEST] ---  end  test_trivial_sat() ---\n\n");
}

/*
Full "trusted solving" run on a tiny unsatisfiable formula:
(id) p cnf 2 4
(1)  1 2 0
(2)  1 -2 0
(3)  -1 2 0
(4)  -1 -2 0
--- proof:
5  1  0  1 2 0
6  -1 0  3 4 0
7     0  5 6 0
*/
void test_trivial_unsat(void) {
    printf("[TEST] --- begin test_trivial_unsat() ---\n");

    // Parse formula and set up trusted checker process
    const char* cnf = "cnf/trivial-unsat.cnf";
    FILE *out_parsed, *out_directives, *in_feedback;
    u64 chkid = setup(cnf, &out_parsed, &out_directives, &in_feedback);

    // PRODUCE
    const int cls_5[1] = {1}; const u64 hints_5[2] = {1, 2};
    produce_cls(out_directives, in_feedback, 5, 1, cls_5, 2, hints_5, 0, 0);
    const int cls_6[1] = {-1}; const u64 hints_6[2] = {3, 4};
    produce_cls(out_directives, in_feedback, 6, 1, cls_6, 2, hints_6, 0, 0);
    const u64 hints_7[2] = {5, 6};
    produce_cls(out_directives, in_feedback, 7, 0, 0, 2, hints_7, 0, 0);

    // VALIDATE_UNSAT
    trusted_utils_write_char(TRUSTED_CHK_VALIDATE_UNSAT, out_directives);
    trusted_utils_write_ul(7, out_directives); // ID of conclusion
    trusted_utils_write_int(0, out_directives); // zero failed assumptions
    await_ok(out_directives, in_feedback);
    u8 unsat_sig[SIG_SIZE_BYTES];
    trusted_utils_read_sig(unsat_sig, in_feedback);

    // (optional) confirm with "confirmer" module
    append_to_trace_file(20, unsat_sig, 0, 0);
    bool ok = confirm(cnf);
    do_assert(ok);

    // TERMINATE
    clean_up(chkid, out_parsed, out_directives, in_feedback);
    printf("[TEST] ---  end  test_trivial_unsat() ---\n\n");
}

/*
Full "trusted solving" run on the same formula as in test_trivial_unsat()
but now with two checker sub-processes at once.
*/
void test_trivial_unsat_x2(void) {
    printf("[TEST] --- begin test_trivial_unsat_x2() ---\n");

    // Parse formula and set up trusted checker processes.
    // (NOTE: We run a parser process for each checker process we initialize.
    // In actual solvers, you probably want to only run a single parser process
    // and use its output for all solvers and checkers.)
    const char* cnf = "cnf/trivial-unsat.cnf";
    FILE *out_parsed_1, *out_directives_1, *in_feedback_1;
    u64 chkid_1 = setup(cnf, &out_parsed_1, &out_directives_1, &in_feedback_1);
    FILE *out_parsed_2, *out_directives_2, *in_feedback_2;
    u64 chkid_2 = setup(cnf, &out_parsed_2, &out_directives_2, &in_feedback_2);
    u32 cidx;

    // PRODUCE
    const int cls_5[1] = {1}; const u64 hints_5[2] = {1, 2}; u8 sig_5[SIG_SIZE_BYTES];
    produce_cls(out_directives_1, in_feedback_1, 5, 1, cls_5, 2, hints_5, sig_5, &cidx);
    const int cls_6[1] = {-1}; const u64 hints_6[2] = {3, 4}; u8 sig_6[SIG_SIZE_BYTES];
    produce_cls(out_directives_2, in_feedback_2, 6, 1, cls_6, 2, hints_6, sig_6, &cidx);

    // DELETE
    const u64 del_ids[4] = {3, 4};
    delete_cls(out_directives_2, in_feedback_2, del_ids, 2);

    // IMPORT
    import_cls(out_directives_1, in_feedback_1, 6, 1, cls_6, sig_6, cidx);
    import_cls(out_directives_2, in_feedback_2, 5, 1, cls_5, sig_5, cidx);

    // PRODUCE
    const u64 hints_7[2] = {5, 6};
    produce_cls(out_directives_1, in_feedback_1, 7, 0, 0, 2, hints_7, 0, 0);

    // VALIDATE_UNSAT
    trusted_utils_write_char(TRUSTED_CHK_VALIDATE_UNSAT, out_directives_1);
    trusted_utils_write_ul(7, out_directives_1); // ID of conclusion
    trusted_utils_write_int(0, out_directives_1); // zero failed assumptions
    await_ok(out_directives_1, in_feedback_1);
    u8 unsat_sig[SIG_SIZE_BYTES];
    trusted_utils_read_sig(unsat_sig, in_feedback_1);

    // (optional) confirm with "confirmer" module
    append_to_trace_file(20, unsat_sig, 0, 0);
    bool ok = confirm(cnf);
    do_assert(ok);
    
    // TERMINATE
    clean_up(chkid_1, out_parsed_1, out_directives_1, in_feedback_1);
    clean_up(chkid_2, out_parsed_2, out_directives_2, in_feedback_2);
    printf("[TEST] ---  end  test_trivial_unsat_x2() ---\n\n");
}

void test_incremental_trivial_unsat(void) {
    printf("[TEST] --- begin test_incremental_trivial_unsat() ---\n");

    // Parse formula and set up trusted checker process
    const char* cnf = "cnf/incremental-unsat.cnf";
    FILE *out_parsed, *out_directives, *in_feedback;
    u64 chkid = setup(cnf, &out_parsed, &out_directives, &in_feedback);

    // PRODUCE
    const int cls_5[1] = {1}; const u64 hints_5[2] = {1, 2};
    produce_cls(out_directives, in_feedback, 5, 1, cls_5, 2, hints_5, 0, 0);

    // VALIDATE_UNSAT
    trusted_utils_write_char(TRUSTED_CHK_VALIDATE_UNSAT, out_directives);
    trusted_utils_write_ul(5, out_directives); // ID of conclusion
    // failed assumptions
    trusted_utils_write_int(1, out_directives); // # failed
    trusted_utils_write_int(-1, out_directives);
    await_ok(out_directives, in_feedback);
    u8 unsat_sig[SIG_SIZE_BYTES];
    trusted_utils_read_sig(unsat_sig, in_feedback);

    // (optional) confirm with "confirmer" module
    int failed = -1;
    append_to_trace_file(20, unsat_sig, 1, &failed);
    bool ok = confirm(cnf);
    do_assert(ok);

    // TERMINATE
    clean_up(chkid, out_parsed, out_directives, in_feedback);
    printf("[TEST] ---  end  test_incremental_trivial_unsat() ---\n\n");
}

// Some macros that reduce the boilerplate code for clause additions.
#define PRODUCE(id,...) \
    u64 I = id; \
    int C[] = {__VA_ARGS__};
#define SEND_WITH_HINTS(...) \
    u64 D[] = {__VA_ARGS__}; \
    const int nc = sizeof(C)/sizeof(C[0]); \
    const int nd = sizeof(D)/sizeof(D[0]); \
    produce_cls(out_directives, in_feedback, I, nc, C, nd, D, 0, 0);
#define PRODUCE_AND_SEND_EMPTY(id,...) \
    u64 D[] = {__VA_ARGS__}; \
    const int nd = sizeof(D)/sizeof(D[0]); \
    produce_cls(out_directives, in_feedback, id, 0, 0, nd, D, 0, 0);

void test_incremental_unsat(void) {
    printf("[TEST] --- begin test_incremental_unsat() ---\n");

    // Parse 1st increment and set up trusted checker process
    const char* cnf = "cnf/incremental-example.cnf";
    FILE *out_parsed, *out_directives, *in_feedback;
    u64 chkid = setup(cnf, &out_parsed, &out_directives, &in_feedback);

    u64 L = INT32_MAX;

    { PRODUCE(L+1, -4, -2, -1) SEND_WITH_HINTS(2, 4) }
    { PRODUCE(L+2, -2, -1) SEND_WITH_HINTS(L+1, 1, 3) }

    // VALIDATE_UNSAT
    trusted_utils_write_char(TRUSTED_CHK_VALIDATE_UNSAT, out_directives);
    trusted_utils_write_ul(L+2, out_directives); // ID of conclusion
    // failed assumptions
    const int failed1[2] = {2, 1};
    trusted_utils_write_int(2, out_directives); // # failed
    trusted_utils_write_ints(failed1, 2, out_directives);
    await_ok(out_directives, in_feedback);
    u8 unsat_sig[SIG_SIZE_BYTES];
    trusted_utils_read_sig(unsat_sig, in_feedback);

    append_to_trace_file(20, unsat_sig, 2, (int*)failed1);

    // Next Increment
    load_next_formula_increment(out_parsed, out_directives, in_feedback);

    // VALIDATE_SAT
    int model[4] = {-1, -2, -3, -4};
    trusted_utils_write_char(TRUSTED_CHK_VALIDATE_SAT, out_directives);
    trusted_utils_write_int(4, out_directives);
    trusted_utils_write_ints(model, 4, out_directives);
    await_ok(out_directives, in_feedback);
    u8 sat_sig[SIG_SIZE_BYTES];
    trusted_utils_read_sig(sat_sig, in_feedback);

    append_to_trace_file(10, sat_sig, 0, 0);

    // Next increment
    load_next_formula_increment(out_parsed, out_directives, in_feedback);

    { PRODUCE(L+3, 2) SEND_WITH_HINTS(5, 7) }
    { PRODUCE(L+4, -1) SEND_WITH_HINTS(L+3, L+2) }
    { PRODUCE_AND_SEND_EMPTY(L+5, L+4, L+3, 6) }

    // VALIDATE_UNSAT
    trusted_utils_write_char(TRUSTED_CHK_VALIDATE_UNSAT, out_directives);
    trusted_utils_write_ul(L+5, out_directives); // ID of conclusion
    trusted_utils_write_int(0, out_directives); // # failed
    await_ok(out_directives, in_feedback);
    trusted_utils_read_sig(unsat_sig, in_feedback);

    append_to_trace_file(20, unsat_sig, 0, 0);

    // (optional) confirm with "confirmer" module
    bool ok = confirm(cnf);
    do_assert(ok);

    // TERMINATE
    clean_up(chkid, out_parsed, out_directives, in_feedback);
    printf("[TEST] ---  end  test_incremental_unsat() ---\n\n");
}

int main(void) {
    test_trivial_sat();
    test_trivial_unsat();
    test_trivial_unsat_x2();
    test_incremental_unsat();
}
