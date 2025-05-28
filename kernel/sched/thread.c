#include "sched/thread.h"
#include "cpu/cpu.h"
#include "events/event.h"
#include "lib/list.h"
#include "memory/vm.h"
#include "sched/process.h"
#include "memory/heap.h"
#include "common/assert.h"
#include "common/log.h"
#include "lib/string.h"
#include "lib/mem.h"
#include "sched/sched.h"
#include "cpu/lapic.h"

#include <stddef.h>
#include <stdint.h>

#define KSTACK_SIZE (4096)



_Atomic uint64_t next_tid = 0;

void maybe_reschedule_thread(Thread* t);
void thread_init([[maybe_unused]] Thread* prev) {
    log_raw("thread_init\n");
    maybe_reschedule_thread(prev);
    //event_handle_next(nullptr);
    ASSERT(!cpu_int_get_state());
    cpu_int_unmask();
}

static Thread* thread_create(Process* proc, const char* name, ThreadStack k_stack, uintptr_t init_sp) {
    Thread* t = kmalloc(sizeof(Thread));
    Event* t_event = kmalloc(sizeof(Event));

    *t = (Thread) {
        .rsp = init_sp,
        .kernel_stack = k_stack,
        .tid = next_tid++,
        .proc = proc,
        .status = STATUS_READY,

        .event = t_event
    };

    strncpy(t->name, name, sizeof(t->name) - 1);

    if (proc != nullptr)
        list_append(&proc->threads, &t->proc_list_node);

    return t;
}

Thread* thread_kernel_create(void (*entry)(), const char* name) {
    uintptr_t k_stack_base = (uintptr_t) vm_map_anon(&kernel_as, nullptr, KSTACK_SIZE, VM_PROT_RW, VM_CACHING_DEFAULT, VM_FLAG_ZERO);
    ASSERT(k_stack_base != 0);
    k_stack_base += KSTACK_SIZE;

    ThreadStack k_stack = { .base = k_stack_base, .size = KSTACK_SIZE };

    KernelInitStack* init_stack = (KernelInitStack*) (k_stack.base - sizeof(KernelInitStack));

    init_stack->entry = entry;
    init_stack->thread_init = thread_init;

    Thread* t = thread_create(nullptr, name, k_stack, (uintptr_t) init_stack);

    logln(LOG_DEBUG, "SCHED", "Created kernel thread (tid: %lu, name %s)", t->tid, t->name);

    return t;
}
