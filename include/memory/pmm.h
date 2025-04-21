#pragma once

#include "common/types.h"
#include "sys/boot.h"

#define PMM_DEFAULT (0)
#define PMM_ZERO (1 << 0)

paddr_t pmm_alloc(uint64_t flags);
void pmm_free(paddr_t pf);
void pmm_init(Memmap* memmap);
