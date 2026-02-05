#pragma once

#include "sched/thread.h"

#include <stddef.h>
#include <stdint.h>

extern uint32_t g_fpu_area_size;

extern void (*fpu_save)(void* area);
extern void (*fpu_restore)(void* area);

void fpu_init_thread_state(void* area);
void fpu_init_features();
void fpu_init_core();
