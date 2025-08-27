
#include <stdint.h>
#include <stdbool.h>        // for false, bool, true
#include <stdio.h>          // for FILE, fgetc_unlocked, fopen, EOF
#include <stdlib.h>         // for abort, free
#include "parser_defs.h"
#include "siphash.h"        // for siphash_digest, siphash_update
#include "sort.h"
#include "trusted_utils.h"  // for trusted_utils_write_int, trusted_utils_wr...
#include "assert.h"
#include "clausecompress.h"

// Instantiate int_vec
#define TYPE int
#define TYPED(THING) int_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

// Instantiate u64_vec
#define TYPE u64
#define TYPED(THING) u64_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

FILE* f_out;

struct u64_vec* clause_vecs_by_len;

struct int_vec* cls_data;
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

int nb_vars = -1;
int nb_cls = -1;


// Comparator for external literals that sorts them according to their internal representations.
int compare_external_lits(const void* a, const void* b) {
    u32 ilit_a = cc_internalize_lit(* (int*) a);
    u32 ilit_b = cc_internalize_lit(* (int*) b);
    return ilit_a - ilit_b;
}

// Handle a fully parsed clause and insert it into the internal data structures.
void process_clause(void) {

    // Where to insert the clause?
    u32 clslen = cls_data->size;
    u32 vec_idx = clslen - 1;
    // Make sure the insertion position exists
    while (vec_idx >= clause_vecs_by_len->size)
        u64_vec_push(clause_vecs_by_len, 0);
    // Make sure that a vector exists at the insertion position
    if (!clause_vecs_by_len->data[vec_idx])
        clause_vecs_by_len->data[vec_idx] = (u64) int_vec_init(16);
    // Retrieve the vector to append to
    struct int_vec* v = (struct int_vec*) clause_vecs_by_len->data[vec_idx];

    // Sort literals in clause ascendingly according to their internal representations
    sort_objs(cls_data->data, clslen, sizeof(int), compare_external_lits);

    // Append clause to the vector of clause literals
    for (u32 i = 0; i < clslen; i++) {
        int_vec_push(v, cls_data->data[i]);
    }

    // Clear buffer for next clause
    int_vec_clear(cls_data);
}

// Outputs the parsed formula in a (semi-)normalized form.
void output_clauses(void) {

    // Iterate over all clause lengths
    for (u32 i = 0; i < clause_vecs_by_len->size; i++) {
        if (!clause_vecs_by_len->data[i]) continue;

        // There are some clauses of this length
        u32 clslen = i+1;
        struct int_vec* v = (struct int_vec*) clause_vecs_by_len->data[i];
        assert(v->size > 0);
        // Sort clauses according to the internal representations of their first literals
        sort_objs(v->data, v->size / clslen, sizeof(int) * clslen, compare_external_lits);

        // Output and fingerprint each clause, with termination zeroes
        int* data = v->data;
        int zero = 0;
        while (data != v->data + v->size) {
            trusted_utils_write_ints(data, clslen, f_out);
            trusted_utils_write_int(zero, f_out);
            siphash_update((u8*) data, clslen * sizeof(int));
            siphash_update((u8*) &zero, sizeof(int)); 
            data += clslen;
        }

        // Free vector associated with this clause length
        int_vec_free(v);
    }
}

void output_assumptions(void) {
    if (asmpt_data->size == 0) return;
    // Assumptions separator
    trusted_utils_write_int(IMPCHECK_MARKER_ASSUMPTIONS, f_out);
    trusted_utils_write_ints(asmpt_data->data, asmpt_data->size, f_out);
}

// Process a single parsed integer in "num".
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
    if (in_assumptions) {
        int_vec_push(asmpt_data, lit);
        if (lit == 0) {
            increment_finished = true;
            in_assumptions = false;
        }
    } else {
        if (lit == 0) process_clause();
        else int_vec_push(cls_data, lit);
    }
    num = 0;
    sign = 1;
    began_num = false;
}

// Process a single read character.
bool tp_inner_process(char c) {

    if (comment && c != '\n' && c != '\r') return false;

    signed char uc = *((signed char*) &c);
    switch (uc) {
    case EOF:
        if (began_num) append_integer();
        increment_finished = true;
        input_finished = true;
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
    cls_data = int_vec_init(64);
    asmpt_data = int_vec_init(64);
    clause_vecs_by_len = u64_vec_init(0);
}

bool tp_inner_input_finished(void) {return input_finished;}
bool tp_inner_input_valid(void) {return !input_invalid;}

void tp_inner_output(void) {
    output_clauses();
}

void tp_inner_end(void) {
    int_vec_free(cls_data);
    u64_vec_free(clause_vecs_by_len);
}
