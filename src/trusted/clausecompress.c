
#include "clausecompress.h"

#include "assert.h"
#include "stdlib.h"
#include "trusted_utils.h"
#include "stdint.h"

// -1 1  -2 2 -3  3 -4  4 -5  5 ...
// v  v  v  v  v  v  v  v  v  v ...
// 0  1  2  3  4  5  6  7  8  9 ...
u32 cc_compress_lit(int elit) {
    return 2 * abs(elit) - 1 - (elit < 0);
}
int cc_decompress_lit(u32 ilit) {
    return (1 + ilit / 2) * (2 * (ilit & 1) - 1);
}

u8 cc_nb_needed_varlength_bytes(u32 x) {
    return 1 + (bool)(x>>7) + (bool)(x>>14) + (bool)(x>>21) + (bool)(x>>28);
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
int qsort_compare(const void* a, const void* b) {
    return *(u32*)a - *(u32*)b;
}
void insertion_sort(u32* arr, int size) {
    for (int i = 1; i < size; i++) {
        u32 key = arr[i];
        int j = i - 1;
        // Move elements of arr[0..i-1] that are greater than key
        // to one position ahead of their current position
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j = j - 1;
        }
        arr[j + 1] = key;
    }
}
int cc_prepare_clause_and_get_compressed_size(int* lits, int nb_lits) {

    // Compress literals in-place and then sort them in increasing order
    for (int i = 0; i < nb_lits; i++) {
        lits[i] = cc_compress_lit(lits[i]);
    }
    //qsort((u32*) lits, nb_lits, sizeof(u32), qsort_compare);
    insertion_sort((u32*) lits, nb_lits);

    // Compute size of the output data with variable-length differential coding
    u32 size = 0;
    u32 last = 0;
    for (int i = 0; i < nb_lits; i++) {
        u32 ilit = lits[i];
        assert(ilit == 0 || ilit > last);
        size += cc_nb_needed_varlength_bytes(ilit - last);
        lits[i] = ilit - last;
        last = ilit;
    }
    // Somewhat awkward to find the correct number of bytes to have enough room for itself ...
    u8 nbBytesForSize = 1;
    if (cc_nb_needed_varlength_bytes(size+nbBytesForSize) > nbBytesForSize)
        nbBytesForSize++;
    assert(cc_nb_needed_varlength_bytes(size+nbBytesForSize) == nbBytesForSize);
    size += nbBytesForSize;
    return size;
}

void cc_compress_and_write_clause(int* lits, int nb_lits, u32 compr_size, u8* out) {
    u32 idx = cc_write_varlength(compr_size, out);
    for (int i = 0; i < nb_lits; i++) {
        idx += cc_write_varlength(lits[i], out+idx);
    }
}

struct cclause_view cc_get_compressed_view(const u8* data) {
    struct cclause_view view;
    view.data = data;
    u32 compr_size;
    u8 offset = cc_read_varlength(view.data, &compr_size);
    view.end = view.data + compr_size;
    view.data += offset;
    view.last = 0;
    return view;
}

bool cc_get_next_decompressed_lit(struct cclause_view* view, int* out) {
    if (MALLOB_UNLIKELY(view->data == view->end)) return false; // done
    assert(view->data < view->end);
    u32 diff;
    view->data += cc_read_varlength(view->data, &diff);
    assert(view->last == 0 || diff > 0);
    u32 ilit = view->last + diff;
    view->last = ilit;
    *out = cc_decompress_lit(ilit);
    return true;
}
