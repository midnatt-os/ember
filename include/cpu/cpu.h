#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    size_t seq_id;
    uint32_t lapic_id;
    uint64_t lapic_timer_freq;
} Cpu;

extern Cpu* cpus;
extern size_t cpu_count;


static inline __seg_gs Cpu* cpu_current() {
    return ((__seg_gs Cpu*) nullptr);
}

[[noreturn]] void cpu_halt();
void cpu_relax();
void cpu_int_mask();
void cpu_int_unmask();
bool cpu_int_get_state();

