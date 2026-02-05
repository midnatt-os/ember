#pragma once

#include "common/lock/spinlock.h"
#include "cpu/tss.h"
#include "sched/sched.h"
#include "sys/timers.h"

#include <stdint.h>

#define CPU_CURRENT ((*(__seg_gs cpu_t*) nullptr).self)

typedef struct cpu {
    struct cpu* self;
    scheduler_t scheduler;
    tss_t* tss;

    uint64_t seq_id;
    uint64_t lapic_id;
    uint64_t lapic_timer_freq;
    uint64_t tlb_gen;

    timer_queue_t timer_queue;
    spinlock_t sched_lock;
} cpu_t;

extern cpu_t* cpus;
extern volatile uint64_t cpu_online_count;
