#pragma once

#include <stddef.h>
#include <stdint.h>

#define PMM_DEFAULT 0
#define PMM_ZERO (1 << 0)

uintptr_t pmm_alloc(uint64_t flags);
void pmm_free(uintptr_t ptr);
uintptr_t pmm_early_alloc(size_t pages);
