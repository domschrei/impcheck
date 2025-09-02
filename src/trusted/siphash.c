
#include "siphash.h"
#include "secret.h"
#include "trusted_utils.h"
#include <stdbool.h>  // for true
#include <stdlib.h>   // for free, abort
#include <assert.h>   // for assert
#include <unistd.h>

struct siphash* sh_copy = 0;

#define cROUNDS 2
#define dROUNDS 4

#define SH_UINT64_C(c) c##UL

#define ROTL(x, b) (u64)(((x) << (b)) | ((x) >> (64 - (b))))

#define U32TO8_LE(p, v)                                                        \
    (p)[0] = (u8)((v));                                                   \
    (p)[1] = (u8)((v) >> 8);                                              \
    (p)[2] = (u8)((v) >> 16);                                             \
    (p)[3] = (u8)((v) >> 24);

#define U64TO8_LE(p, v)                                                        \
    U32TO8_LE((p), (unsigned int)((v)));                                           \
    U32TO8_LE((p) + 4, (unsigned int)((v) >> 32));

#define U8TO64_LE(p)                                                           \
    (((u64)((p)[0])) | ((u64)((p)[1]) << 8) |                        \
     ((u64)((p)[2]) << 16) | ((u64)((p)[3]) << 24) |                 \
     ((u64)((p)[4]) << 32) | ((u64)((p)[5]) << 40) |                 \
     ((u64)((p)[6]) << 48) | ((u64)((p)[7]) << 56))

#define SIPROUND                                                               \
    do {                                                                       \
        sh->v0 += sh->v1;                                                              \
        sh->v1 = ROTL(sh->v1, 13);                                                     \
        sh->v1 ^= sh->v0;                                                              \
        sh->v0 = ROTL(sh->v0, 32);                                                     \
        sh->v2 += sh->v3;                                                              \
        sh->v3 = ROTL(sh->v3, 16);                                                     \
        sh->v3 ^= sh->v2;                                                              \
        sh->v0 += sh->v3;                                                              \
        sh->v3 = ROTL(sh->v3, 21);                                                     \
        sh->v3 ^= sh->v0;                                                              \
        sh->v2 += sh->v1;                                                              \
        sh->v1 = ROTL(sh->v1, 17);                                                     \
        sh->v1 ^= sh->v2;                                                              \
        sh->v2 = ROTL(sh->v2, 32);                                                     \
    } while (0)

void process_next_block(struct siphash* sh) {
    sh->m = U8TO64_LE(sh->buf);
    sh->v3 ^= sh->m;
    for (int i = 0; i < cROUNDS; ++i)
        SIPROUND;
    sh->v0 ^= sh->m;
}

void process_final_block(struct siphash* sh) {
    const int left = sh->inlen & 7;
    assert(left == sh->buflen);
    u64 b = ((u64)sh->inlen) << 56;
    u8* ni = sh->buf;

    switch (left) {
    case 7:
        b |= ((u64)ni[6]) << 48;
        /* FALLTHRU */
    case 6:
        b |= ((u64)ni[5]) << 40;
        /* FALLTHRU */
    case 5:
        b |= ((u64)ni[4]) << 32;
        /* FALLTHRU */
    case 4:
        b |= ((u64)ni[3]) << 24;
        /* FALLTHRU */
    case 3:
        b |= ((u64)ni[2]) << 16;
        /* FALLTHRU */
    case 2:
        b |= ((u64)ni[1]) << 8;
        /* FALLTHRU */
    case 1:
        b |= ((u64)ni[0]);
        break;
    case 0:
        break;
    }

    sh->v3 ^= b;

    for (int i = 0; i < cROUNDS; ++i)
        SIPROUND;

    sh->v0 ^= b;

    if (sh->outlen == 16)
        sh->v2 ^= 0xee;
    else
        sh->v2 ^= 0xff;

    for (int i = 0; i < dROUNDS; ++i)
        SIPROUND;

    b = sh->v0 ^ sh->v1 ^ sh->v2 ^ sh->v3;
    U64TO8_LE(sh->out, b);

    sh->v1 ^= 0xdd;

    for (int i = 0; i < dROUNDS; ++i)
        SIPROUND;

    b = sh->v0 ^ sh->v1 ^ sh->v2 ^ sh->v3;
    U64TO8_LE(sh->out + 8, b);
}

struct siphash* siphash_init(const unsigned char* key_128bit) {
    struct siphash* sh = trusted_utils_malloc(sizeof(struct siphash));
    sh->outlen = 128 / 8;
    sh->buflen = 0;
    sh->out = trusted_utils_malloc(128 / 8);
    sh->buf = trusted_utils_malloc(8);
    siphash_reinit(sh, key_128bit);
    return sh;
}
void siphash_reinit(struct siphash* sh, const unsigned char* key_128bit) {
    sh->kk = key_128bit;
    siphash_reset(sh);
}
void siphash_reset(struct siphash* sh) {
    sh->v0 = SH_UINT64_C(0x736f6d6570736575);
    sh->v1 = SH_UINT64_C(0x646f72616e646f6d);
    sh->v2 = SH_UINT64_C(0x6c7967656e657261);
    sh->v3 = SH_UINT64_C(0x7465646279746573);
    sh->k0 = U8TO64_LE(sh->kk);
    sh->k1 = U8TO64_LE(sh->kk + 8);
    sh->v3 ^= sh->k1;
    sh->v2 ^= sh->k0;
    sh->v1 ^= sh->k1;
    sh->v0 ^= sh->k0;
    sh->inlen = 0;
    sh->buflen = 0;
    if (sh->outlen == 16)
        sh->v1 ^= 0xee;
}
struct siphash* siphash_copy(struct siphash* sh) {
    if (!sh_copy) {
        sh_copy = siphash_init(SECRET_KEY);
    }
    u8* out = sh_copy->out;
    u8* buf = sh_copy->buf;
    trusted_utils_copy_bytes((void*) sh_copy, (void*) sh, sizeof(struct siphash));
    sh_copy->out = out;
    sh_copy->buf = buf;
    trusted_utils_copy_bytes(sh_copy->out, sh->out, 128 / 8);
    trusted_utils_copy_bytes(sh_copy->buf, sh->buf, 8);
    return sh_copy;
}
SIG_TYPE siphash_end_branch(struct siphash* sh, int nb_padding_zeroes) {
    struct siphash* copy = siphash_copy(sh);
    if (nb_padding_zeroes > 0) siphash_pad(copy, nb_padding_zeroes);
    return siphash_digest(copy);
}
void siphash_update(struct siphash* sh, const unsigned char* data, u64 nb_bytes) {
    u32 datapos = 0;
    while (true) {
        while (sh->buflen < 8u && datapos < nb_bytes) {
            sh->buf[sh->buflen++] = data[datapos++];
        }
        if (sh->buflen < 8u) {
            break;
        }
        process_next_block(sh);
        sh->buflen = 0;
    }
    sh->inlen += nb_bytes;
}
void siphash_pad(struct siphash* sh, u64 nb_bytes) {
    const unsigned char c = 0;
    for (u64 i = 0; i < nb_bytes; i++) siphash_update(sh, &c, 1);
}
SIG_TYPE siphash_digest(struct siphash* sh) {
    process_final_block(sh);
    return * (SIG_TYPE*) sh->out;
}
void siphash_free(struct siphash* sh) {
    free(sh->buf);
    free(sh->out);
    free(sh);
}

#undef SH_UINT64_C
