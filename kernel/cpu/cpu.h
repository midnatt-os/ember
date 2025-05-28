#pragma once

#include "lib/list.h"
#include "sched/sched.h"
#include "cpu/tss.h"

#include <stdint.h>
#include <stddef.h>

typedef struct Cpu {
    struct Cpu* self;

    size_t seq_id;
    uint32_t lapic_id;
    uint64_t lapic_timer_freq;

    Tss* tss;

    Scheduler* scheduler;
    List events;
} Cpu;

extern Cpu* cpus;
extern size_t cpu_count;

// Future me's problem. Race condition between preemption and using this.
static inline Cpu* cpu_current() {
    return ((__seg_gs Cpu*) nullptr)->self;
}

static inline bool cpu_is_bsp() {
    return cpu_current()->seq_id == 0;
}

[[noreturn]] void cpu_halt();
void cpu_relax();
bool cpu_int_get_state();
bool cpu_int_mask();
void cpu_int_unmask();
void cpu_int_restore(bool state);
