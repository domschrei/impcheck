
#include "secret.h"
#include "salt.h"
#include "trusted_utils.h"
#include <stdlib.h>

unsigned char SECRET_KEY[] = {
    86, 93, 1, 209, 112, 176, 13, 40,
    168, 223, 25, 22, 134, 58, 21, 211
};

unsigned long rng_state = IMPCHECK_SALT;
unsigned long next_random_ul(void) {
    rng_state += 0x9e3779b97f4a7c15;
    unsigned long z = rng_state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

void generate_key(const char* seed_str) {
    char *endptr;
    u64 seed = strtoul(seed_str, &endptr, 10);
    rng_state += seed;
    for (unsigned int i = 0; i < 16; i += sizeof(unsigned long)) {
        unsigned long r = next_random_ul();
        trusted_utils_copy_bytes(SECRET_KEY + i, (const u8*) &r, sizeof(unsigned long));
    }
}
