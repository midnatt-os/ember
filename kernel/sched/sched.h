#pragma once

#include "sched/thread.h"
#include "sys/timers.h"

typedef struct {
    thread_t* current_thread;
    thread_t* idle_thread;
    list_t ready_queue;
    bool need_resched;
    timer_t preempt_timer;
} scheduler_t;

void sched_yield(thread_status_t target_status);
void sched_sleep(uint64_t duration);
void sched_wake_thread(thread_t* thread);
void sched_maybe_reschedule(thread_t* thread);
void sched_schedule_thread(thread_t* thread);

void sched_init_cpu();
