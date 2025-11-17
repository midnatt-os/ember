#include "sched/sched.h"

#include "common/asm.h"
#include "common/assert.h"
#include "common/limine_requests.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/tsc.h"
#include "lib/container.h"
#include "lib/list.h"
#include "limine.h"
#include "mem/heap.h"
#include "mem/page.h"
#include "mem/slab.h"
#include "sched/thread.h"
#include "sys/modules.h"
#include "sys/time.h"
#include "sys/timers.h"

#include <stdint.h>

#define SCHED (CPU_CURRENT->scheduler)

#define SCHED_QUANTUM_NS ms_to_ns(3)


thread_t* sched_context_switch(thread_t* this, thread_t* next);

static void idle() {
    while (true) {
        __asm__ __volatile__("sti; hlt" ::: "memory");
    }
}

static void preempt_callback([[maybe_unused]] void* arg) {
    SCHED.need_resched = true;
}

void sched_schedule_thread(thread_t* thread) {
    ASSERT(thread->cpu_id == CPU_CURRENT->seq_id);
    bool prev_if = int_mask();
    thread->status = STATUS_READY;
    list_append(&CPU_CURRENT->scheduler.ready_queue, &thread->sched_list_node);
    int_restore(prev_if);
}

void sched_maybe_reschedule(thread_t* thread) {
    ASSERT(!int_state());
    ASSERT(thread->cpu_id == CPU_CURRENT->seq_id);

    if (thread == SCHED.idle_thread)
        return;

    switch (thread->status) {
        case STATUS_READY:
        case STATUS_RUNNING: sched_schedule_thread(thread); break;

        case STATUS_DONE:
        case STATUS_BLOCKED: break;
    }
}

static thread_t* pick_next_thread() {
    if (!list_peek(&CPU_CURRENT->scheduler.ready_queue))
        return nullptr;

    return CONTAINER_OF(list_pop(&CPU_CURRENT->scheduler.ready_queue), thread_t, sched_list_node);
}

static void switch_threads(thread_t* this, thread_t* next) {
    [[maybe_unused]] thread_t* old = sched_context_switch(this, next);
    // sched_maybe_reschedule(old);
}

void sched_yield(thread_status_t target_status) {
    bool prev_if = int_mask();

    thread_t* prev = SCHED.current_thread;

    // 1) If still runnable, enqueue BEFORE picking next
    if (prev != SCHED.idle_thread) {
        if (target_status == STATUS_READY)
            sched_maybe_reschedule(prev);
        else
            prev->status = target_status; // BLOCKED / DONE
    }

    // 2) Pick next (or idle)
    thread_t* next = pick_next_thread();
    if (!next)
        next = SCHED.idle_thread;

    // 3) No-op switch
    if (next == prev) {
        if (next != SCHED.idle_thread)
            timer_mod(&SCHED.preempt_timer, SCHED_QUANTUM_NS);
        else if (rb_node_is_linked(&SCHED.preempt_timer.node))
            timer_cancel(&SCHED.preempt_timer);
        int_restore(prev_if);
        return;
    }

    // 4) Commit
    SCHED.current_thread = next;
    next->status = STATUS_RUNNING;

    // log_raw("CPU%lu switch (this:%s -> next:%s)\n", CPU_CURRENT->seq_id, prev->name, next->name);
    //   FLYTTA TIMER TILL EFTER ? ;
    if (next != SCHED.idle_thread)
        timer_mod(&SCHED.preempt_timer, SCHED_QUANTUM_NS);
    else if (rb_node_is_linked(&SCHED.preempt_timer.node))
        timer_cancel(&SCHED.preempt_timer);

    switch_threads(prev, next);
    int_restore(prev_if);
}

void sched_wake_thread(thread_t* thread) {
    bool prev_if = int_mask();
    ASSERT(thread->cpu_id == CPU_CURRENT->seq_id);

    if (thread->status == STATUS_BLOCKED) {
        sched_schedule_thread(thread);
        SCHED.need_resched = true;
    }

    int_restore(prev_if);
}

void sched_sleep(uint64_t duration) {
    bool prev_if = int_mask();

    thread_t* cur = SCHED.current_thread;
    cur->status = STATUS_BLOCKED;
    timer_mod(&cur->sleep_timer, duration);

    sched_yield(STATUS_BLOCKED); // already marked blocked
    int_restore(prev_if);
}

void one() {
    while (true) {
        log_raw("one\n");
        sched_sleep(s_to_ns(1));
    }
}

void two() {
    log_raw("two\n");
    sched_sleep(s_to_ns(5));
}

void sched_init_cpu() {
    thread_t* idle_thread = thread_create_kernel("idle", idle);
    thread_t* bsp_thread = heap_alloc(sizeof(thread_t));
    *bsp_thread = (thread_t) {
        .tid = 0,
        .name = "bsp",
        .status = STATUS_DONE,
        .cpu_id = CPU_CURRENT->seq_id,
    };

    SCHED = (scheduler_t) {
        .current_thread = bsp_thread,
        .idle_thread = idle_thread,
        .ready_queue = LIST_NEW,
        .need_resched = false,
        .preempt_timer = timer_create(preempt_callback, nullptr),
    };

    // TODO: MUTEX; CREATION/REAPING/PROPER; PROCESSES/USERSPACE
    // TODO LOG IN EVERY FUCKING CALL TO FIX HARDWARE ISSUES(WAKE, SLEEP, CTX_SWITCH, SWITCH TO SAME, IDLE STUFF)

    module_t fireworks;
    struct limine_file* fw_lim = find_limine_module("firework_test.mod");
    ASSERT(fw_lim);
    module_load(fw_lim->address, fw_lim->size, &fireworks);

    fireworks.init();

    sched_yield(STATUS_DONE);
    ASSERT_UNREACHABLE();
}
