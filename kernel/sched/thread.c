#include "sched/thread.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
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

#define KSTACK_SIZE (4096 * 2)
#define USTACK_SIZE (4096 * 4)

typedef struct [[gnu::packed]] {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    void (*thread_init_common)(Thread* prev);
    void (*entry)();
} KernelInitStack;

typedef struct [[gnu::packed]] {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    void (*thread_init_common)(Thread* prev);
    void (*thread_init_user)();
    uint64_t user_rip, user_cs, user_rflags, user_rsp, user_ss;
} UserInitStack;



_Atomic uint64_t next_tid = 0;

static ThreadStack create_thread_stack(size_t stack_size) {
    void* k_stack_base = vm_map_anon(&kernel_as, nullptr, stack_size, VM_PROT_RW, VM_CACHING_DEFAULT, VM_FLAG_ZERO);
    if (k_stack_base == nullptr)
        return (ThreadStack) { .base = 0, .size = 0 };

    return (ThreadStack) { .base = (uintptr_t) k_stack_base + stack_size, .size = stack_size };
}

void maybe_reschedule_thread(Thread* t);
void thread_init_common([[maybe_unused]] Thread* prev) {
    log_raw("thread_init\n");
    maybe_reschedule_thread(prev);
    event_handle_next(nullptr); // This basically rearms the timer since the first time a thread runs it never finishes the event handler (I think)
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
    ThreadStack k_stack = create_thread_stack(KSTACK_SIZE);
    ASSERT(k_stack.size != 0);

    KernelInitStack* init_stack = (KernelInitStack*) (k_stack.base - sizeof(KernelInitStack));

    init_stack->thread_init_common = thread_init_common;
    init_stack->entry = entry;

    Thread* t = thread_create(nullptr, name, k_stack, (uintptr_t) init_stack);

    logln(LOG_DEBUG, "SCHED", "Created kernel thread (tid: %lu, name %s)", t->tid, t->name);

    return t;
}

extern void thread_init_user();
Thread* thread_create_user(Process* proc, const char* name, uintptr_t entry_rip) {
    // Create user stack
    void* user_stack = vm_map_anon(proc->as, nullptr, USTACK_SIZE, VM_PROT_RW, VM_CACHING_DEFAULT, VM_FLAG_NONE);
    ASSERT(user_stack != nullptr);

    // Create Kernel / initial stack;
    ThreadStack k_stack = create_thread_stack(KSTACK_SIZE);
    ASSERT(k_stack.size != 0);

    UserInitStack* init_stack = (UserInitStack*) (k_stack.base - sizeof(UserInitStack));

    init_stack->thread_init_common = thread_init_common;
    init_stack->thread_init_user = thread_init_user;

    init_stack->user_rip = entry_rip;
    init_stack->user_cs = GDT_SELECTOR_CODE64_RING3;
    init_stack->user_rflags = 0x202; // reserved | IF
    init_stack->user_rsp = (uintptr_t) user_stack + USTACK_SIZE;
    init_stack->user_ss = GDT_SELECTOR_DATA64_RING3;

    Thread* t = thread_create(proc, name, k_stack, (uintptr_t) init_stack);

    logln(LOG_DEBUG, "SCHED", "Created user thread (tid: %lu, name %s)", t->tid, t->name);

    return t;
}
