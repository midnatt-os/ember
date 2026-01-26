#pragma once

#include "common/lock/spinlock.h"
#include "sched/thread.h"
#include "sys/timers.h"

typedef struct {
    thread_t* current_thread;
    thread_t* idle_thread;

    thread_t* reaper_thread;
    timer_t reap_timer;
    list_t reap_queue;
    spinlock_t reap_lock;

    list_t ready_queue;
    bool need_resched;
    timer_t preempt_timer;
} scheduler_t;

thread_t* sched_get_current_thread();

void sched_yield(thread_status_t target_status);
void sched_sleep(uint64_t duration);
void sched_wake_thread(void* thread);
void sched_maybe_reschedule(thread_t* thread);
void sched_schedule_thread(thread_t* thread);

void sched_init_cpu();
