
#pragma once

#include "trusted_utils.h"
#include <stdbool.h>

// Modifies the data at "lits". Returns the size of the compressed clause in bytes
// (for cc_compress_and_write_clause). 
int cc_prepare_clause_and_get_compressed_size(int* lits, int nb_lits);

// Takes lits manipulated by cc_prepare_clause_and_get_compressed_size and outputs
// the compressed representation to out.
void cc_compress_and_write_clause(int* lits, int nb_lits, u32 compr_size, u8* out);

// Helper struct for iterating over a compressed clause.
struct cclause_view {
    const u8* data;
    const u8* end;
    u32 last;
};
// Returns a helper struct for iterating over a compressed clause.
struct cclause_view cc_get_compressed_view(const u8* data);
// Perform an iteration step over the provided helper struct.
bool cc_get_next_decompressed_lit(struct cclause_view* view, int* out);

// If we compress clauses, each clause is a byte array or a single 64-bit data bundle
// disguised as a pointer. Otherwise, each clause is a zero-terminated int array.
#if IMPCHECK_COMPRESS
#define CLSTYPE u8*
#else
#define CLSTYPE int*
#endif

// Allocate and initialize a new clause.
CLSTYPE clause_init(int* data, int nb_lits);

// Returns a struct for iterating over a compressed clause or a fabricated pointer
// with internal clause literals.
struct cclause_view get_cclause_view(const u8** cls);

// Convenience macro for iterating over the literals of a clause (in whichever representation).
#if IMPCHECK_COMPRESS
#define FOR_LIT_IN_CLAUSE(C, L) \
        struct cclause_view view_ ## C ## _ ## L = get_cclause_view((const CLSTYPE*) &C); \
        for (int L; cc_get_next_decompressed_lit(&view_ ## C ## _ ## L, &L); )
#else
#define FOR_LIT_IN_CLAUSE(C, L) \
        for ( \
            int idx_ ## C ## _ ## L = 0, L = C[idx_ ## C ## _ ## L]; \
            L != 0; \
            idx_ ## C ## _ ## L += 1, L = C[idx_ ## C ## _ ## L] \
        )
#endif

// Check whether clauses are equivalent.
bool clauses_equivalent(const CLSTYPE left_cls, const CLSTYPE right_cls);
