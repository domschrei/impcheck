
#include "pointer_storage.h"
#include <assert.h>
#include <stdio.h>

const u64 MASK_MSBIT = 0x80UL << 56;
const u64 MASK_MSBYTE = 0xffUL << 56;
#define IS_REAL_POINTER(x) (( ((u64) x) & MASK_MSBYTE ) != MASK_MSBIT)

// Creates a fake 64-bit pointer that carries up to seven bytes of payload
// and has its most significant byte set to 0b1000'0000, indicating that
// it is not a real pointer. Can be unpacked via ptr_storage_get.
void* ptr_storage_create(u8* data) {
    u64 dataAsU64 = MASK_MSBIT | * (u64*) data; // MSB set to 1, others to 0: indicates that this is not a "real" pointer
    //for (u8 i = 0; i < 7; i++) dataAsU64 |= ((u64)data[i]) << 8*i;
    //printf("storing short clause as pointer %p\n", (void*)dataAsU64);
    return (void*) dataAsU64;
}

bool ptr_storage_is_real_pointer(const void* ptr) {
    return IS_REAL_POINTER(ptr);
}

// Checks whether the pointed-to pointer is a fake pointer.
// If it is, the pointed-to pointer is modified to contain the raw data
// that had been provided in the corresponding call to ptr_storage_create.
const void* ptr_storage_get(const void** ptr) {
    if (IS_REAL_POINTER(*ptr)) return *ptr; // no indicator set
    // indicator is set: data is stored in here directly
    *ptr = (const void*) ( ((u64) *ptr) & ~MASK_MSBYTE );
    return (const void*) ptr;
}
