#pragma once

#include <stddef.h>

#include "boot.h"

void load_kernel_symbols(const Module* modules, size_t module_count);
void log_stack_trace();
