#pragma once

#include "stdint.h"

#define HHDM(addr) (g_hhdm_offset + addr)
#define PHYS_FROM_HHDM(addr) (addr - g_hhdm_offset)

extern uintptr_t g_hhdm_offset;
