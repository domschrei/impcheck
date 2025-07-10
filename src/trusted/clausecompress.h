
#pragma once

#include "trusted_utils.h"
#include <stdbool.h>

u32 cc_compress_lit(int elit);
int cc_decompress_lit(u32 ilit);

u8 cc_nb_needed_bytes(int x);
u8 cc_nb_needed_varlength_bytes(int x);

u8 cc_write_fixed_bytes_int(int x, u8 nb_bytes, u8* out);
u8 cc_read_fixed_bytes_int(u8* in, u8 nb_bytes, int* out);

u8 cc_write_varlength(u32 n, u8* out);
u8 cc_read_varlength(const u8* in, u32* out);

int cc_prepare_clause_and_get_compressed_size(int* lits, int nb_lits);
void cc_compress_and_write_clause(int* lits, int nb_lits, u32 compr_size, u8* out);

bool cc_get_next_decompressed_lit(const u8* data, u32* compr_size, u32* idx, u32* last, int* out);
