
#include "trusted_parser.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>        // for false, bool, true
#include <stdio.h>          // for FILE, fgetc_unlocked, fopen, EOF
#include <stdlib.h>         // for abort, free
#include "parser_defs.h"
#include "siphash.h"        // for siphash_digest, siphash_update
#include "trusted_utils.h"  // for trusted_utils_write_int, trusted_utils_wr...

// Instantiate int_vec
#define TYPE int
#define TYPED(THING) int_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

FILE* f_out;

struct int_vec* data;
struct int_vec* asmpt_data;

bool comment = false;
bool header = false;
bool in_assumptions = false;
bool increment_finished = false;
bool input_finished = false;
bool input_invalid = false;
bool began_num = false;

int num = 0;
int sign = 1;
int nb_read_cls = 0;

int nb_vars = -1;
int nb_cls = -1;


void output_literal_buffer(void) {
    siphash_update((unsigned char*) data->data, data->size * sizeof(int));
    trusted_utils_write_ints(data->data, data->size, f_out);
    if (tp_input_log()) {
        for (u32 i = 0; i < data->size; i++) {
            int lit = data->data[i];
            fprintf(tp_input_log(), "%i%c", lit, lit==0 ? '\n' : ' ');
        }
    }
    int_vec_clear(data);
}

void append_integer(void) {
    if (header) {
        if (nb_vars == -1) {
            nb_vars = num;
            trusted_utils_write_int(nb_vars, f_out);
        } else if (nb_cls == -1) {
            nb_cls = num;
            trusted_utils_write_int(nb_cls, f_out);
            header = false;
        } else abort();
        num = 0;
        began_num = false;
        return;
    }

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
        //header = true;
        //break;
    case 'c':
        if (!header) comment = true;
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
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
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

void tp_inner_init(FILE* f) {
    f_out = f;
    data = int_vec_init(TRUSTED_CHK_MAX_BUF_SIZE);
    asmpt_data = int_vec_init(64);
}

bool tp_inner_input_finished(void) {return input_finished;}
bool tp_inner_input_valid(void) {return !input_invalid;}

void tp_inner_output(void) {
    if (data->size > 0) output_literal_buffer();
    if (asmpt_data->size == 0) int_vec_push(asmpt_data, 0);
    // Assumptions separator
    trusted_utils_write_int(IMPCHECK_MARKER_ASSUMPTIONS, f_out);
    trusted_utils_write_ints(asmpt_data->data, asmpt_data->size, f_out);
    if (tp_input_log()) {
        fprintf(tp_input_log(), "a");
        for (u32 i = 0; i < asmpt_data->size; i++)
            fprintf(tp_input_log(), " %i", asmpt_data->data[i]);
        fprintf(tp_input_log(), "\n");
    }
    // clear for next increment
    int_vec_clear(asmpt_data);
}

void tp_inner_end(void) {
    int_vec_free(data);
    int_vec_free(asmpt_data);
}
