
#include <stdlib.h>

#include "keygen.h"
#include "secret.h"
#include "salt.h"
#include "siphash.h"
#include "trusted_utils.h"

unsigned long rng_state = IMPCHECK_SALT;
unsigned long next_random_ul(void) {
    rng_state += 0x9e3779b97f4a7c15;
    unsigned long z = rng_state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

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
    siphash_init(init_key);
    siphash_update(SECRET_KEY, 16);
    u8* fp = siphash_digest();

    // Overwrite SECRET_KEY with generated fingerprint
    for (unsigned int i = 0; i < 16; i++) {
        SECRET_KEY[i] = fp[i];
    }

    // Re-initialize SipHash, now with the actual key
    siphash_reinit(SECRET_KEY);
}
