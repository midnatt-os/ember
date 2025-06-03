#pragma once

#include <stdint.h>

extern uint32_t fpu_area_size;
extern void (*fpu_save)(void *area);
extern void (*fpu_restore)(void *area);

void fpu_init();
void fpu_init_cpu();
