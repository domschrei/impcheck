
#pragma once

#include "trusted_utils.h"

struct siphash {
    const unsigned char* kk;
    u8* out;
    int outlen;
    u64 v0, v1, v2, v3;
    u64 k0, k1;
    u64 m;
    u64 inlen;
    u8* buf;
    unsigned char buflen;
};

struct siphash* siphash_init(const unsigned char* key_128bit);
void siphash_reinit(struct siphash* sh, const unsigned char* key_128bit);
void siphash_reset(struct siphash*);
SIG_TYPE siphash_end_branch(struct siphash* sh, int nb_padding_zeroes);

void siphash_update(struct siphash*, const unsigned char* data, u64 nb_bytes);
void siphash_pad(struct siphash*, u64 nb_bytes);
SIG_TYPE siphash_digest(struct siphash*);
void siphash_free(struct siphash*);
