
#include "trusted_utils.h"
#include "siphash.h"

void confirm_result(u8* f_sig, u8 constant, int witness_size, const int* witness_data, u8* out) {
    siphash_reset();
    siphash_update(&constant, 1);
    siphash_update(f_sig, SIG_SIZE_BYTES);
    siphash_update((const u8*) witness_data, sizeof(int)*witness_size);
    u8* sig = siphash_digest();
    trusted_utils_copy_bytes(out, sig, SIG_SIZE_BYTES);
}
