#pragma once

#include "events/event.h"
#include "lib/list.h"
#include "sched/thread.h"
#include <stdint.h>

typedef struct {
    Thread* current_thread;
    List ready_queue;
    Thread* idle_thread;
    Event* preemption_event;
    bool should_yield;
    ThreadStatus yield_status;
} Scheduler;

Thread* sched_get_current_thread();
Process* sched_get_current_process();

void sched_schedule_thread(Thread* t);
void sched_preempt();
void sched_yield(ThreadStatus target_status);
void sched_sleep(uint64_t ns);
void sched_init();
void sched_start();
