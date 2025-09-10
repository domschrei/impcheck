
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "parser_defs.h"
#include "secret.h"
#include "signature_trace.h"
#include "siphash.h"
#include "trusted_parser.h"
#include "trusted_utils.h"
#include "confirm.h"

// Instantiate int_vec
#define TYPE int
#define TYPED(THING) int_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

FILE* f;
FILE* f_out;
FILE* inputlog_out;

struct int_vec* data;
struct int_vec* asmpt_data;

bool comment = false;
bool in_assumptions = false;
bool increment_finished = false;
bool input_finished = false;
bool input_invalid = false;
bool began_num = false;

int num = 0;
int sign = 1;
u32 nb_read_cls = 0;

bool confirm;

int revision = -1;
struct siphash* siphash_parser;

void output_literal_buffer(void) {
    siphash_update(siphash_parser, (unsigned char*) data->data, data->size * sizeof(int));
    trusted_utils_write_ints(data->data, data->size, f_out);
    if (inputlog_out) {
        for (u32 i = 0; i < data->size; i++) {
            int lit = data->data[i];
            fprintf(inputlog_out, "%i%c", lit, lit==0 ? '\n' : ' ');
        }
    }
    int_vec_clear(data);
}

void append_integer(void) {
    const int lit = sign * num;
    num = 0;
    sign = 1;
    began_num = false;

    if (in_assumptions) {
        int_vec_push(asmpt_data, lit);
        if (lit == 0) {
            increment_finished = true;
            in_assumptions = false;
        }
        return;
    }
    if (lit == 0) nb_read_cls++;
    if (data->size == data->capacity) output_literal_buffer();
    int_vec_push(data, lit);
}

bool tp_inner_process(char c) {
    if (comment && c != '\n' && c != '\r') return false;

    signed char uc = *((signed char*) &c);
    switch (uc) {
    case EOF:
        if (began_num) append_integer();
        increment_finished = true;
        input_finished = true;
        break;
    case '\n':
    case '\r':
        comment = false;
        if (began_num) append_integer();
        in_assumptions = false;
        break;
    case 'p':
    case 'c':
        comment = true;
        break;
    case 'a':
        assert(!in_assumptions);
        in_assumptions = true;
        break;
    case ' ':
        if (began_num) append_integer();
        break;
    case '-':
        sign = -1;
        began_num = true;
        break;
    case'0':case'1':case'2':case'3':case'4':case'5':case'6':case'7':case'8':case'9':
        // Add digit to current number
        num = num*10 + (c-'0');
        began_num = true;
        break;
    default:
        break;
    }

    if (increment_finished) {
        increment_finished = false;
        return true;
    }
    return false;
}

void tp_inner_output(void) {
    if (data->size > 0) output_literal_buffer();
    if (asmpt_data->size == 0) int_vec_push(asmpt_data, 0);
    // Assumptions separator
    trusted_utils_write_int(IMPCHECK_MARKER_ASSUMPTIONS, f_out);
    trusted_utils_write_ints(asmpt_data->data, asmpt_data->size, f_out);
    if (inputlog_out) {
        fprintf(inputlog_out, "a");
        for (u32 i = 0; i < asmpt_data->size; i++)
            fprintf(inputlog_out, " %i", asmpt_data->data[i]);
        fprintf(inputlog_out, "\n");
        fflush(inputlog_out);
    }
    // clear for next increment
    int_vec_clear(asmpt_data);
}

void tp_init(const char* filename, FILE* out, bool confirm_results, FILE* inputlog) {
    f = fopen(filename, "r");
    inputlog_out = inputlog;
    siphash_parser = siphash_init(SECRET_KEY);
    f_out = out;
    data = int_vec_init(TRUSTED_CHK_MAX_BUF_SIZE);
    asmpt_data = int_vec_init(64);
    confirm = confirm_results;
}

bool parse_increment(void) {
    revision++;

    struct sig_obligation* item = 0;
    if (confirm && !signature_trace_get_next(&item)) {
        trusted_utils_log_err("Missing or malformed signature obligation!");
        printf("s NOT VERIFIED\n");
        return false;
    }

    // Read formula increment
    while (true) {
        int c_int = UNLOCKED_IO(fgetc)(f);
        if (tp_inner_process((char) c_int)) break;
    }
    if (input_invalid) return false;

    // Output increment with fingerprint
    tp_inner_output();

    SIG_TYPE sig_for = siphash_end_branch(siphash_parser, 0);
    trusted_utils_write_sig((u8*) &sig_for, f_out);
    trusted_utils_write_int(IMPCHECK_MARKER_ENDOFINCREMENT, f_out);
    UNLOCKED_IO(fflush)(f_out);

    // If a result with signature is provided, confirm it
    if (confirm) {
        if (nb_read_cls != item->cidx) {
            snprintf(trusted_utils_msgstr, 512, "Unexpected clause index %u (read until index %u)!", item->cidx, nb_read_cls);
            trusted_utils_log_err(trusted_utils_msgstr);
            printf("s NOT VERIFIED\n");
            return false;
        }

        if (item->res == 0) {
            printf("UNKNOWN cidx=%u rev=%i\n", item->cidx, revision);
            return true;
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
    while (!input_finished) {
        if (!parse_increment()) break;
    }
    return input_finished && !input_invalid;
}

void tp_end(void) {
    int_vec_free(data);
    int_vec_free(asmpt_data);
}
