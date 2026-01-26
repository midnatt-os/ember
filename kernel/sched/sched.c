#include "sched/sched.h"

#include "common/asm.h"
#include "common/assert.h"
#include "common/limine_requests.h"
#include "common/lock/mutex.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/tsc.h"
#include "flanterm.h"
#include "lib/container.h"
#include "lib/list.h"
#include "limine.h"
#include "mem/heap.h"
#include "mem/page.h"
#include "mem/ptm.h"
#include "mem/slab.h"
#include "sched/thread.h"
#include "sys/modules.h"
#include "sys/time.h"
#include "sys/timers.h"

#include <nanoprintf.h>
#include <stdint.h>

#define SCHED (CPU_CURRENT->scheduler)

#define SCHED_QUANTUM_NS ms_to_ns(3)

thread_t* sched_context_switch(thread_t* this, thread_t* next);

static void idle() {
    while (true) {
        __asm__ __volatile__("sti; hlt" ::: "memory");
    }
}

static void reap() {
    while (true) {
        bool prev = spinlock_lock(&SCHED.reap_lock);

        // uint64_t reap_count = 0;

        list_node_t* thread_node = list_pop(&SCHED.reap_queue);
        while (thread_node) {
            thread_t* thread = CONTAINER_OF(thread_node, thread_t, reap_list_node);
            ASSERT(thread->status == STATUS_DONE);
            thread_destroy(thread);
            thread_node = list_pop(&SCHED.reap_queue);
            // reap_count++;
        }

        // logln(LOG_DEBUG, "REAPER", "Reaped %lu threads.", reap_count);

        spinlock_unlock(&SCHED.reap_lock, prev);

        sched_yield(STATUS_BLOCKED);
    }
}

static void preempt_callback(void* _) {
    SCHED.need_resched = true;
}

thread_t* sched_get_current_thread() {
    return SCHED.current_thread;
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
    ASSERT(thread != SCHED.idle_thread);

    switch (thread->status) {
        case STATUS_BLOCKED: break;

        case STATUS_READY:
        case STATUS_RUNNING: sched_schedule_thread(thread); break;

        case STATUS_DONE: {
            bool prev = spinlock_lock(&SCHED.reap_lock);
            list_append(&SCHED.reap_queue, &thread->reap_list_node);
            spinlock_unlock(&SCHED.reap_lock, prev);

            if (!SCHED.reap_timer.armed)
                timer_mod(&SCHED.reap_timer, ms_to_ns(500));

            break;
        }
    }
}

static thread_t* pick_next_thread() {
    if (!list_peek(&CPU_CURRENT->scheduler.ready_queue))
        return nullptr;

    return CONTAINER_OF(list_pop(&CPU_CURRENT->scheduler.ready_queue), thread_t, sched_list_node);
}

static void switch_threads(thread_t* this, thread_t* next) {
    [[maybe_unused]] thread_t* old = sched_context_switch(this, next);
}

void sched_yield(thread_status_t target_status) {
    bool prev_if = int_mask();
    thread_t* prev = SCHED.current_thread;

    if (prev != SCHED.idle_thread) {
        prev->status = target_status;
        sched_maybe_reschedule(prev);
    }

    thread_t* next = pick_next_thread();
    if (!next)
        next = SCHED.idle_thread;

    if (next != SCHED.idle_thread)
        timer_mod(&SCHED.preempt_timer, SCHED_QUANTUM_NS);
    else
        timer_cancel(&SCHED.preempt_timer);

    if (next == prev) {
        int_restore(prev_if);
        return;
    }

    SCHED.current_thread = next;
    next->status = STATUS_RUNNING;
    switch_threads(prev, next);

    int_restore(prev_if);
}

void sched_wake_thread(void* thread) {
    thread_t* t = (thread_t*) thread;
    bool prev_if = int_mask();
    ASSERT(t->status != STATUS_DONE);
    ASSERT(t->cpu_id == CPU_CURRENT->seq_id);

    if (t->status == STATUS_BLOCKED) {
        sched_schedule_thread(t);
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

extern uint64_t pf_total_count;
extern uint64_t pf_use_count;
extern struct flanterm_context* ft_ctx;
static void mem_info(void* _) {
    while (true) {
        logln(LOG_DEBUG, "MEM", "%lu / %lu MiB used.", pf_use_count / 256, pf_total_count / 256);
        flanterm_full_refresh(ft_ctx);
        sched_sleep(s_to_ns(5));
    }
}

void sched_init_cpu() {
    char* idle_name_str = heap_alloc(sizeof("idle") + sizeof(char));
    npf_snprintf(idle_name_str, sizeof("idle") + sizeof(char), "idle%lu", CPU_CURRENT->seq_id);
    thread_t* idle_thread = thread_create_kernel(idle_name_str, idle);

    char* reaper_name_str = heap_alloc(sizeof("reaper") + sizeof(char));
    npf_snprintf(reaper_name_str, sizeof("reaper") + sizeof(char), "reaper%lu", CPU_CURRENT->seq_id);
    thread_t* reaper_thread = thread_create_kernel(reaper_name_str, reap);
    reaper_thread->status = STATUS_BLOCKED;

    char* bsp_name_str = heap_alloc(sizeof("bsp") + sizeof(char));
    npf_snprintf(bsp_name_str, sizeof("bsp") + sizeof(char), "bsp%lu", CPU_CURRENT->seq_id);
    thread_t* bsp_thread = thread_create_kernel(bsp_name_str, nullptr);
    bsp_thread->status = STATUS_DONE;

    SCHED = (scheduler_t) {
        .current_thread = bsp_thread,
        .idle_thread = idle_thread,
        .reaper_thread = reaper_thread,
        .reap_timer = timer_create((timer_fn_t) sched_wake_thread, reaper_thread),
        .reap_queue = LIST_NEW,
        .reap_lock = SPINLOCK_NEW,
        .ready_queue = LIST_NEW,
        .need_resched = false,
        .preempt_timer = timer_create(preempt_callback, nullptr),
    };

    if (CPU_CURRENT->seq_id == 0) {
        thread_t* mem_info_thread = thread_create_kernel("mem_usage", mem_info);
        sched_schedule_thread(mem_info_thread);
    }

    sched_yield(STATUS_DONE);
    ASSERT_UNREACHABLE();
}
