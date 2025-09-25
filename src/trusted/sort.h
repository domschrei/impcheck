
#pragma once

#include "trusted_utils.h"

void sort_ints(int* ints, u32 nb_ints);
void sort_uints(u32* ints, u32 nb_ints);
void sort_objs(void* objs, u32 nb_objs, u32 bytes_per_obj, int (*compare)(const void *, const void *));
