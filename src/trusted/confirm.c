
#include "secret.h"
#include "trusted_utils.h"
#include "siphash.h"

struct siphash* siphash_confirm = 0;

SIG_TYPE confirm_result(SIG_TYPE f_sig, u8 constant, int witness_size, const int* witness_data) {
    if (!siphash_confirm) {
        siphash_confirm = siphash_init(SECRET_KEY);
    }
    siphash_reset(siphash_confirm);
    siphash_update(siphash_confirm, &constant, 1);
    siphash_update(siphash_confirm, (u8*) &f_sig, SIG_SIZE_BYTES);
    siphash_update(siphash_confirm, (const u8*) witness_data, sizeof(int)*witness_size);
    return siphash_digest(siphash_confirm);
}
