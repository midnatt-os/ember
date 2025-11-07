#pragma once

#include <stdint.h>

void load_kernel_symbols();
void log_stack_trace();
uintptr_t kernel_symbol_lookup(const char* name);
