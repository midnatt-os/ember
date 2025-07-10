#include "sched/sched.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/fpu.h"
#include "cpu/interrupts.h"
#include "cpu/lapic.h"
#include "cpu/registers.h"
#include "cpu/tss.h"
#include "events/event.h"
#include "lib/list.h"
#include "memory/vm.h"
#include "sched/process.h"
#include "sched/thread.h"
#include "common/assert.h"
#include "sys/time.h"
#include "memory/heap.h"
#include "lib/mem.h"
#include "lib/string.h"
#include "nanoprintf.h"

#include <stdint.h>

#define THREAD_QUANTUM (ms_to_ns(5))

#define SCHED (cpu_current()->scheduler)

Thread* sched_get_current_thread() {
    bool prev = cpu_int_mask();
    Thread* t = SCHED->current_thread;
    cpu_int_restore(prev);
    return t;
}

Process* sched_get_current_process() {
    return sched_get_current_thread()->proc;
}

static void preempt(void* target_status) {
    SCHED->should_yield = true;
    SCHED->yield_status = (ThreadStatus) (uint64_t) target_status;
}

void sched_preempt() {
    event_cancel(SCHED->preemption_event);

    *SCHED->preemption_event = (Event) {
        .deadline = time_current() + THREAD_QUANTUM,
        .callback = preempt,
        .callback_arg = (void*) STATUS_READY,
        .list_node = {}
    };

    event_add(SCHED->preemption_event);
}

void sched_schedule_thread(Thread* t) {
    t->status = STATUS_READY;

    bool prev = cpu_int_mask();
    list_append(&SCHED->ready_queue, &t->sched_list_node);
    cpu_int_restore(prev);
}

void maybe_reschedule_thread(Thread* t) {
    ASSERT(!cpu_int_get_state());

    if (t != SCHED->idle_thread) {
        switch (t->status) {
            case STATUS_READY: sched_schedule_thread(t); break;
            case STATUS_RUNNING: break;
            case STATUS_DONE: break; // TODO: REAP
            case STATUS_BLOCKED: break;
            default: ASSERT_UNREACHABLE();
        }
    }

    sched_preempt();
}

Thread* sched_context_switch(Thread* this, Thread* next);
static void sched_switch(Thread* this, Thread* next) {
    if (next->proc != nullptr)
        vm_load_address_space(next->proc->as);
    else
        vm_load_address_space(&kernel_as);

    cpu_current()->scheduler->current_thread = next;
    tss_set_rsp0(cpu_current()->tss, next->kernel_stack.base);

    this->state.gs = read_msr(MSR_GS_KERNEL_BASE);
    this->state.fs = read_msr(MSR_FS_BASE);

    write_msr(MSR_GS_KERNEL_BASE, next->state.gs);
    write_msr(MSR_FS_BASE, next->state.fs);

    if (this->proc != nullptr)
        fpu_save(this->state.fpu);

    if (next->proc != nullptr)
        fpu_restore(next->state.fpu);

    log_raw("from: %s ; to: %s\n", this->name, next->name);

    [[maybe_unused]] Thread* prev = sched_context_switch(this, next);
    maybe_reschedule_thread(prev);
}

static Thread* choose_next_thread() {
    if (list_is_empty(&SCHED->ready_queue))
        return nullptr;

    return LIST_ELEMENT_OF(list_pop(&SCHED->ready_queue), Thread, sched_list_node);
}

void sched_yield(ThreadStatus target_status) {
    ASSERT(target_status != STATUS_RUNNING);
    bool prev_state = cpu_int_mask();
    ASSERT(!cpu_int_get_state());

    Thread* this = SCHED->current_thread;
    Thread* next = choose_next_thread();

    if (next == nullptr) {
        if (target_status == STATUS_READY) {
            sched_preempt();
            cpu_int_restore(prev_state);
            return;
        }

        next = SCHED->idle_thread;
    }

    this->status = target_status;
    next->status = STATUS_RUNNING;

    SCHED->current_thread = next;

    sched_switch(this, next);
    cpu_int_restore(prev_state);
}

static void wake(void* thread) {
    sched_schedule_thread(thread);
    sched_yield(STATUS_READY);
}

void sched_sleep(uint64_t ns) {
    Thread* t = sched_get_current_thread();

    *t->sleep_event = (Event) {
        .deadline = time_current() + ns,
        .callback = wake,
        .callback_arg = t,
        .list_node = {}
    };

    event_add(t->sleep_event);
    sched_yield(STATUS_BLOCKED);
}

[[noreturn]] static void sched_idle() {
    while (true) {
        log_raw("cpu%lu idling\n", cpu_current()->seq_id);
        uint64_t n = time_current() + s_to_ns(1);
        while (time_current() < n);
        cpu_relax();
    }
}

void sched_init() {
    Scheduler* sched = kmalloc(sizeof(Scheduler));
    sched->preemption_event = kmalloc(sizeof(Event));
    sched->ready_queue = LIST_NEW;

    cpu_current()->scheduler = sched;

    char name[16];
    npf_snprintf(name, sizeof(name), "idle_cpu%lu", cpu_current()->seq_id);

    sched->idle_thread = thread_kernel_create(sched_idle, name);
}

void sched_start() {
    logln(LOG_INFO, "SCHED", "Starting scheduler");

    Thread* bsp_thread = kmalloc(sizeof(Thread));
    memclear(bsp_thread, sizeof(Thread));

    *bsp_thread = (Thread) {
        .tid = next_tid++,
        .sleep_event = kmalloc(sizeof(Event)),
        .name = "bsp",
        .status = STATUS_DONE
    };

    cpu_current()->scheduler->current_thread = bsp_thread;

    sched_yield(STATUS_DONE);
    ASSERT_UNREACHABLE();
}
