
#pragma once

#include "trusted_utils.h"

void tp_init(const char* filename, FILE* out);
bool tp_parse(u8** sig);
void tp_end();
