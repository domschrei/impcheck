
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

    // Parse key seed
    char *endptr;
    u64 seed = strtoul(seed_str, &endptr, 10);

    // Initialize SipHash with default key, generate fingerprint of key seed + salt
    siphash_init(SECRET_KEY);
    siphash_update((const u8*) &seed, sizeof(seed));
    u64 salt = IMPCHECK_SALT;
    siphash_update((const u8*) &salt, sizeof(salt));
    u8* fp = siphash_digest();

    // Replace default key with generated fingerprint
    for (unsigned int i = 0; i < 16; i++) {
        SECRET_KEY[i] = fp[i];
    }
}
