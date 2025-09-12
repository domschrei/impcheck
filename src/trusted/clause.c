
#include "clause.h"

#include "assert.h"
#include "pointer_storage.h"
#include "sort.h"
#include "stdlib.h"
#include "trusted_utils.h"
#include "stdint.h"

// -1 1  -2 2 -3  3 -4  4 -5  5 ...
// v  v  v  v  v  v  v  v  v  v ...
// 0  1  2  3  4  5  6  7  8  9 ...
u32 cc_internalize_lit(int elit) {
    return 2 * abs(elit) - 1 - (elit < 0);
}
int cc_externalize_lit(u32 ilit) {
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

int cc_prepare_clause_and_get_compressed_size(int* lits, int nb_lits) {

    // Internalize literals in-place and then sort them in increasing order
    for (int i = 0; i < nb_lits; i++) {
        lits[i] = cc_internalize_lit(lits[i]);
    }
    sort_ints(lits, nb_lits);

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
    while (cc_nb_needed_varlength_bytes(size+nbBytesForSize) > nbBytesForSize)
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
    *out = cc_externalize_lit(ilit);
    return true;
}


CLSTYPE clause_init(int* data, int nb_lits) {
#if IMPCHECK_COMPRESS
    u8* out;
    int size = cc_prepare_clause_and_get_compressed_size(data, nb_lits);
    if (size <= 7) {
        u8 inlineCls[8] = {0};
        cc_compress_and_write_clause(data, nb_lits, size, inlineCls);
        out = ptr_storage_create(inlineCls);
    } else {
        out = trusted_utils_calloc(size, 1);
        cc_compress_and_write_clause(data, nb_lits, size, out);
    }
    return out;
#else
    int* cls = trusted_utils_calloc(nb_lits+1, sizeof(int));
    for (int i = 0; i < nb_lits; i++) cls[i] = data[i];
    cls[nb_lits] = 0;
    return cls;
#endif
}

struct cclause_view get_cclause_view(const u8** cls) {
    struct cclause_view view;
    const void* data = ptr_storage_get((const void**) cls);
    view = cc_get_compressed_view((const u8*) data);
    return view;
}

bool clauses_equivalent(const CLSTYPE left_cls, const CLSTYPE right_cls) {
    if (!left_cls || !right_cls) return false;
#if IMPCHECK_COMPRESS
    if (ptr_storage_is_real_pointer(left_cls) != ptr_storage_is_real_pointer(right_cls)) return false;
    if (!ptr_storage_is_real_pointer(left_cls)) {
        // Fabricated pointers: Traverse and check for exactly the same data
        u8* left = (u8*) &left_cls;
        u8* right = (u8*) &right_cls;
        for (u32 i = 0; i < sizeof(void*); i++) {
            if (left[i] != right[i]) return false;
        }
        return true;
    }
    // Linear pass over compressed clause bytes, since they are "normalized" by compression
    int idx = 0;
    while (true) {
        if (left_cls[idx] == 0) return right_cls[idx] == 0;
        if (right_cls[idx] == 0) return false;
        if (left_cls[idx] != right_cls[idx]) return false;
        idx++;
    }
    return true;
#else
    // Quadratic check for clause equivalence -
    // assuming that most imported clauses are rather short.
    int lit_idx = 0;
    for (; left_cls[lit_idx] != 0; lit_idx++) {
        const int left_lit = left_cls[lit_idx];
        bool found = false;
        for (int right_lit_idx = 0; right_cls[right_lit_idx] != 0; right_lit_idx++) {
            if (right_cls[right_lit_idx] == left_lit) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    const int left_size = lit_idx;
    for (lit_idx = 0; right_cls[lit_idx] != 0; lit_idx++) {}
    const int right_size = lit_idx;
    return left_size == right_size;
#endif
}
