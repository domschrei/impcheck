
#pragma once

#include "trusted_utils.h"

void* ptr_storage_create(u8* data);
bool ptr_storage_is_real_pointer(const void* ptr);
const void* ptr_storage_get(const void** ptr);
