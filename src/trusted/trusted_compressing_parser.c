
#include <stdbool.h>        // for false, bool, true
#include <stdio.h>          // for FILE, fgetc_unlocked, fopen, EOF
#include <stdlib.h>         // for abort, free
#include "secret.h"         // for SECRET_KEY
#include "siphash.h"        // for siphash_digest, siphash_init, siphash_update
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

FILE* f;
FILE* f_out;

struct u64_vec* clause_vecs_by_len;

struct int_vec* cls_data;

bool comment = false;
bool header = false;
bool input_finished = false;
bool input_invalid = false;
bool began_num = false;

int num = 0;
int sign = 1;

int nb_vars = -1;
int nb_cls = -1;


int compare_uncompressed_lits(const void* a, const void* b) {
    u32 compr_a = cc_compress_lit(* (int*) a);
    u32 compr_b = cc_compress_lit(* (int*) b);
    return compr_a - compr_b;
}

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

    sort_objs(cls_data->data, clslen, sizeof(int), compare_uncompressed_lits);

    // Append to the vector
    for (u32 i = 0; i < clslen; i++) {
        int_vec_push(v, cls_data->data[i]);
    }
    int_vec_clear(cls_data);
}

void output_clauses(void) {

    for (u32 i = 0; i < clause_vecs_by_len->size; i++) {
        if (!clause_vecs_by_len->data[i]) continue;

        struct int_vec* v = (struct int_vec*) clause_vecs_by_len->data[i];
        assert(v->size > 0);

        // Output and fingerprint each clause, with termination zeroes
        u32 clslen = i+1;
        int* data = v->data;
        int zero = 0;
        while (data != v->data + v->size) {
            trusted_utils_write_ints(data, clslen, f_out);
            trusted_utils_write_int(zero, f_out);
            siphash_update((u8*) data, clslen * sizeof(int));
            siphash_update((u8*) &zero, sizeof(int)); 
            data += clslen;
        }

        int_vec_free(v);
    }
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
    if (lit == 0) process_clause();
    else int_vec_push(cls_data, lit);
    num = 0;
    sign = 1;
    began_num = false;
}

bool process(char c) {

    if (comment && c != '\n' && c != '\r') return false;

    signed char uc = *((signed char*) &c);
    switch (uc) {
    case EOF:
        input_finished = true;
        return true;
    case '\n':
    case '\r':
        comment = false;
        if (began_num) append_integer();
        break;
    case 'p':
        header = true;
        break;
    case 'c':
        if (!header) comment = true;
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
    return false;
}


void tp_init(const char* filename, FILE* out) {
    siphash_init(SECRET_KEY);
    f = fopen(filename, "r");
    f_out = out;
    cls_data = int_vec_init(64);
    clause_vecs_by_len = u64_vec_init(0);
}

void tp_end(void) {
    int_vec_free(cls_data);
    u64_vec_free(clause_vecs_by_len);
}

bool tp_parse(u8** sig) {
    while (true) {
        int c_int = UNLOCKED_IO(fgetc)(f);
        if (process((char) c_int)) break;
    }
    if (began_num) append_integer();
    output_clauses();
    siphash_pad(2); // two-byte padding for formula signature input
    *sig = siphash_digest();
    trusted_utils_write_sig(*sig, f_out);
    return input_finished && !input_invalid;
}
