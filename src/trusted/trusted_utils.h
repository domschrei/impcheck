
#pragma once

#include <features.h>  // for _DEFAULT_SOURCE
#include <stdbool.h>   // for bool
#include <stdio.h>     // for FILE

#ifdef _MSC_VER
#    define MALLOB_LIKELY(condition) condition
#    define MALLOB_UNLIKELY(condition) condition
#else
#    define MALLOB_LIKELY(condition) __builtin_expect(condition, 1)
#    define MALLOB_UNLIKELY(condition) __builtin_expect(condition, 0)
#endif

#if /* glibc >= 2.19: */ _DEFAULT_SOURCE || /* glibc <= 2.19: */ _SVID_SOURCE || _BSD_SOURCE
#define UNLOCKED_IO(fun) fun##_unlocked
#else
#define UNLOCKED_IO(fun) fun
#endif

typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned char u8;

#define SIG_SIZE_BYTES 16
typedef u8 signature[SIG_SIZE_BYTES];
#define TRUSTED_CHK_MAX_BUF_SIZE (1<<14)

extern char trusted_utils_errmsg[512];

void trusted_utils_log(const char* msg);
void trusted_utils_log_err(const char* msg);

void trusted_utils_exit_eof();

bool trusted_utils_begins_with(const char* str, const char* prefix);

void trusted_utils_copy_bytes(u8* to, const u8* from, u64 nb_bytes);
bool trusted_utils_equal_signatures(const u8* left, const u8* right);

bool trusted_utils_read_bool(FILE* file);
int trusted_utils_read_char(FILE* file);
int trusted_utils_read_int(FILE* file);
void trusted_utils_read_ints(int* data, u64 nb_ints, FILE* file);
u64 trusted_utils_read_ul(FILE* file);
void trusted_utils_read_uls(u64* data, u64 nb_uls, FILE* file);
void trusted_utils_read_sig(u8* out_sig, FILE* file);

void trusted_utils_write_bool(bool b, FILE* file);
void trusted_utils_write_char(char c, FILE* file);
void trusted_utils_write_int(int i, FILE* file);
void trusted_utils_write_ints(const int* data, u64 nb_ints, FILE* file);
void trusted_utils_write_ul(u64 u, FILE* file);
void trusted_utils_write_uls(u64* data, u64 nb_uls, FILE* file);
void trusted_utils_write_sig(const u8* sig, FILE* file);

