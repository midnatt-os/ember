#pragma once

#define GDT_SELECTOR_CODE64_RING0 (1 << 3)
#define GDT_SELECTOR_DATA64_RING0 (2 << 3)
#define GDT_SELECTOR_DATA64_RING3 ((3 << 3) | 0b11)
#define GDT_SELECTOR_CODE64_RING3 ((4 << 3) | 0b11)

void gdt_init();
