#pragma once

#include <stdint.h>

#define CPU_CURRENT (*(__seg_gs cpu_t*) nullptr)

typedef struct cpu {
    struct cpu* self;
    uint64_t seq_id;
    uint64_t lapic_id;
    uint64_t lapic_timer_freq;
} cpu_t;
