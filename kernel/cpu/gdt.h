#pragma once

#include "cpu/tss.h"

#define GDT_SEL_CODE_CPL0 (1 << 3)
#define GDT_SEL_DATA_CPL0 (2 << 3)
#define GDT_SEL_DATA_CPL3 ((3 << 3) | 0b11)
#define GDT_SEL_CODE_CPL3 ((4 << 3) | 0b11)

void gdt_load_tss(tss_t* tss);

void gdt_init();
