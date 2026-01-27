
#include <stdlib.h>

#include "keygen.h"
#include "secret.h"
#include "salt.h"
#include "siphash.h"
#include "trusted_utils.h"

void generate_key(const char* seed_str) {

    // Obtain key seed and salt
    char *endptr;
    u64 seed = strtoul(seed_str, &endptr, 10);
    u64 salt = IMPCHECK_SALT;

    // Create an initial key for SipHash based on the seed and salt
    u8 init_key[16];
    trusted_utils_copy_bytes(init_key+0, (const u8*) &seed, 8);
    trusted_utils_copy_bytes(init_key+8, (const u8*) &salt, 8);

    // Initialize SipHash with init_key, generate fingerprint of SECRET_KEY
    struct siphash* sh = siphash_init(init_key);
    siphash_update(sh, SECRET_KEY, 16);
    SIG_TYPE sig = siphash_digest(sh);
    siphash_free(sh);

    // Overwrite SECRET_KEY with generated fingerprint
    trusted_utils_copy_bytes(SECRET_KEY, (u8*) &sig, SIG_SIZE_BYTES);
}
