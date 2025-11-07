#pragma once

#include <stdint.h>

#define PMM_DEFAULT 0
#define PMM_ZERO (1 << 0)

uintptr_t pmm_alloc(uint64_t flags);
void pmm_free(uintptr_t ptr);
void pmm_init();
