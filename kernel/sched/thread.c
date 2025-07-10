#include "sched/thread.h"
#include "common/util.h"
#include "cpu/cpu.h"
#include "cpu/fpu.h"
#include "cpu/gdt.h"
#include "events/event.h"
#include "lib/list.h"
#include "memory/hhdm.h"
#include "memory/pmm.h"
#include "memory/ptm.h"
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

#define KSTACK_SIZE (4096 * 4) // TODO ??? GUARD???
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

// rcx: return rip; r11: rflags
typedef struct [[gnu::packed]] {
    KernelInitStack kinit;
    SyscallSavedRegs regs;
    uintptr_t syscall_rsp;
} ForkStack;

_Atomic uint64_t next_tid = 0;

static ThreadStack create_thread_stack(size_t stack_size) {
    void* k_stack_base = vm_map_anon(&kernel_as, nullptr, stack_size, VM_PROT_RW, VM_CACHING_DEFAULT, VM_FLAG_ZERO);
    if (k_stack_base == nullptr)
        return (ThreadStack) { .base = 0, .size = 0 };

    return (ThreadStack) { .base = (uintptr_t) k_stack_base + stack_size, .size = stack_size };
}

void maybe_reschedule_thread(Thread* t);
void thread_init_common([[maybe_unused]] Thread* prev) {
    log_raw("thread_init: %s\n", sched_get_current_thread()->name);
    maybe_reschedule_thread(prev);
    sched_preempt();
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

        .sleep_event = t_event
    };

    if (proc != nullptr)
        t->state.fpu = vm_map_anon(&kernel_as, nullptr, ALIGN_UP(fpu_area_size, PAGE_SIZE), VM_PROT_RW, VM_CACHING_DEFAULT, VM_FLAG_NONE);

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
Thread* thread_create_user(Process* proc, const char* name, uintptr_t entry_rip, uintptr_t user_sp) {
    // Create Kernel / initial stack;
    ThreadStack k_stack = create_thread_stack(KSTACK_SIZE);

    ptm_virt_to_phys(&kernel_as, k_stack.base - KSTACK_SIZE);
    ptm_virt_to_phys(&kernel_as, k_stack.base - KSTACK_SIZE + PAGE_SIZE);
    ptm_virt_to_phys(&kernel_as, k_stack.base - KSTACK_SIZE + 2 * PAGE_SIZE);
    ptm_virt_to_phys(&kernel_as, k_stack.base - KSTACK_SIZE + 3 * PAGE_SIZE);

    ASSERT(k_stack.size != 0);

    volatile UserInitStack* init_stack = (UserInitStack*) (k_stack.base - sizeof(UserInitStack));

    init_stack->thread_init_common = thread_init_common;
    init_stack->thread_init_user = thread_init_user;

    init_stack->user_rip = entry_rip;
    init_stack->user_cs = GDT_SELECTOR_CODE64_RING3;
    init_stack->user_rflags = 0x202; // reserved | IF
    init_stack->user_rsp = user_sp;
    init_stack->user_ss = GDT_SELECTOR_DATA64_RING3;

    Thread* t = thread_create(proc, name, k_stack, (uintptr_t) init_stack);

    logln(LOG_DEBUG, "SCHED", "Created user thread (tid: %lu, name %s)", t->tid, t->name);

    return t;
}

extern void fork_ret();
Thread* thread_clone(Process* new_proc, Thread* t_to_clone, SyscallSavedRegs* regs) {
    ThreadStack k_stack = create_thread_stack(KSTACK_SIZE);
    ASSERT(k_stack.size != 0);

    ForkStack* init_stack = (ForkStack*) (k_stack.base - sizeof(ForkStack));

    init_stack->kinit.thread_init_common = thread_init_common;
    init_stack->kinit.entry = fork_ret;

    init_stack->regs = *regs;
    init_stack->syscall_rsp = t_to_clone->syscall_rsp;

    Thread* t = thread_create(new_proc, t_to_clone->name, k_stack, (uintptr_t) init_stack);

    memcpy(t->state.fpu, t_to_clone->state.fpu, fpu_area_size);

    t->state.fs = t_to_clone->state.fs;
    t->state.gs = t_to_clone->state.gs;

    return t;
}
