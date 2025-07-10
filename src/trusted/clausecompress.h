
#pragma once

#include "trusted_utils.h"
#include <stdbool.h>

u32 cc_compress_lit(int elit);
int cc_decompress_lit(u32 ilit);

u8 cc_nb_needed_varlength_bytes(u32 x);

u8 cc_write_varlength(u32 n, u8* out);
u8 cc_read_varlength(const u8* in, u32* out);

int cc_prepare_clause_and_get_compressed_size(int* lits, int nb_lits);
void cc_compress_and_write_clause(int* lits, int nb_lits, u32 compr_size, u8* out);

struct cclause_view {
    const u8* data;
    const u8* end;
    u32 last;
};
struct cclause_view cc_get_compressed_view(const u8* data);
bool cc_get_next_decompressed_lit(struct cclause_view* view, int* out);
