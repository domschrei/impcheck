
#include "clausecompress.h"

#include "assert.h"
#include "stdlib.h"

// -1 1  -2 2 -3  3 -4  4 -5  5 ...
// v  v  v  v  v  v  v  v  v  v ...
// 0  1  2  3  4  5  6  7  8  9 ...
u32 cc_compress_lit(int elit) {
    return 2 * abs(elit) - 1 - (elit < 0);
}
int cc_decompress_lit(u32 ilit) {
    return (1 + ilit / 2) * (ilit % 2 == 1 ? 1 : -1);
}

u8 cc_nb_needed_bytes(int x) {
    return 1 + (x >= 128) + (x >= 32768) + (x >= 8388608);
}
u8 cc_nb_needed_varlength_bytes(int x) {
    return 1 + (x >= 128) + (x >= 16384) + (x >= 2097152) + (x >= 268435456);
}

u8 cc_write_fixed_bytes_int(int x, u8 nb_bytes, u8* out) {
    u8 idx = 0;
    if (nb_bytes >= 4) out[idx++] = ((x >> 24) & 0xff);
    if (nb_bytes >= 3) out[idx++] = ((x >> 16) & 0xff);
    if (nb_bytes >= 2) out[idx++] = ((x >> 8) & 0xff);
    if (nb_bytes >= 1) out[idx++] = ((x >> 0) & 0xff);
    return nb_bytes;
}
u8 cc_read_fixed_bytes_int(u8* in, u8 nb_bytes, int* out) {
    *out = 0;
    u8 inIdx = 0;
    if (nb_bytes >= 4) *out += 16777216 * in[inIdx++];
    if (nb_bytes >= 3) *out += 65536 * in[inIdx++];
    if (nb_bytes >= 2) *out += 256 * in[inIdx++];
    if (nb_bytes >= 1) *out += 1 * in[inIdx++];
    return nb_bytes;
}

u8 cc_write_varlength(u32 n, u8* out) {
    u8 idx = 0;
    if (n == 0) {
        out[idx++] = 0;
        return idx;
    }
    u8 ch;
    while (n & ~0x7f) {
        ch = (n & 0x7f) | 0x80;
        out[idx++] = ch;
        n >>= 7;
    }
    ch = n;
    out[idx++] = ch;
    return idx;
}
u8 cc_read_varlength(const u8* in, u32* out) {
    u8 idx = 0;
    *out = 0;
    int32_t coefficient = 1;
    int32_t tmp = in[idx++];
    while (tmp != 0) {
        // continuation bit set?
        if (tmp & 0x80) {
            *out += coefficient * (tmp & 0x7f); // remove first bit
        } else {
            // last byte
            *out += coefficient * tmp; // first bit is 0, so can leave it
            break;
        }
        coefficient *= 128; // 2^7 because we essentially have 7-bit bytes
        tmp = in[idx++];
    }
    return idx;
}

// sort unsigned integers in increasing order
int qsort_compare( const void* a, const void* b) {
    u32 int_a = * ( (u32*) a );
    u32 int_b = * ( (u32*) b );
    if ( int_a == int_b ) return 0;
    else if ( int_a < int_b ) return -1;
    else return 1;
}

int cc_prepare_clause_and_get_compressed_size(int* lits, int nb_lits) {

    // Compress literals in-place and then sort them in increasing order
    for (int i = 0; i < nb_lits; i++) {
        int ilit = cc_compress_lit(lits[i]);
        lits[i] = ilit;
    }
    qsort(lits, nb_lits, sizeof(int), qsort_compare);

    // Compute size of the output data with variable-length differential coding
    int size = 0;
    int diff = 0;
    for (int i = 0; i < nb_lits; i++) {
        int ilit = lits[i];
        size += cc_nb_needed_varlength_bytes(ilit - diff);
        lits[i] = ilit - diff;
        diff = ilit;
    }
    size += cc_nb_needed_varlength_bytes(size);
    return size;
}

void cc_compress_and_write_clause(int* lits, int nb_lits, u32 compr_size, u8* out) {
    int idx = cc_write_varlength(compr_size, out);
    for (int i = 0; i < nb_lits; i++) {
        int ilitWithDiff = lits[i];
        idx += cc_write_varlength(ilitWithDiff, out+idx);
    }
}

bool cc_get_next_decompressed_lit(const u8* data,
        u32* compr_size, u32* idx, u32* last,
        int* out) {

    if (*idx == 0) {
        *idx += cc_read_varlength(data + *idx, compr_size);
    }
    if (*idx == *compr_size) return false; // done
    u32 ilit;
    *idx += cc_read_varlength(data + *idx, &ilit);
    ilit += *last;
    *last = ilit;
    *out = cc_decompress_lit(ilit);
    return true;
}
