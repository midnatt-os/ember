#include "thread.h"

#include "common/asm.h"
#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "lib/mem.h"
#include "mem/heap.h"
#include "mem/page.h"
#include "mem/pmm.h"
#include "mem/ptm.h"
#include "mem/slab.h"
#include "mem/vm.h"
#include "sched/sched.h"
#include "sys/timers.h"

#include <stddef.h>
#include <stdint.h>

#define K_THREAD_STACK_SIZE (4 * PAGE_SIZE)

typedef struct {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    void (*trampoline)();
    void (*fn)();
    void (*exit)();
} kernel_init_stack_t;


object_cache_t* thread_cache = nullptr; // TODO: .slab_caches linker section registration and init

uint64_t next_tid = 0;

void thread_exit() {
    int_mask();

    thread_t* self = CPU_CURRENT->scheduler.current_thread;

    timer_cancel(&self->sleep_timer);

    sched_yield(STATUS_DONE);
    ASSERT_UNREACHABLE();
}

static void kernel_thread_trampoline([[maybe_unused]] thread_t* old_thread) {
    int_unmask();
}

void thread_destroy(thread_t* thread) {
    ASSERT(!thread->sleep_timer.armed);

    // DEBUG: verify region matches exactly the stack
    vm_unmap(&global_as, thread->kstack_base, thread->kstack_size);
    /*for (size_t off = 0; off < thread->kstack_size; off += PAGE_SIZE) {
        uintptr_t va = (uintptr_t) thread->kstack_base + off;
        uintptr_t pa = ptm_virt_to_phys(&global_as, va);
        ASSERT(pa != 0);

        ptm_unmap(&global_as, va, PAGE_SIZE);
        invlpg(va);
        page_ref_dec(pa);
        pmm_free(pa);
        }*/
    slab_free(thread_cache, thread);
}

thread_t* thread_create_kernel(char* name, void* entry_fn) {
    thread_t* t = slab_alloc(thread_cache);
    ASSERT(t);

    void* stack = vm_map_anon(&global_as, 0, K_THREAD_STACK_SIZE, 0, VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_ZERO);
    ASSERT(stack);

    memset(stack, 0xDE, K_THREAD_STACK_SIZE);

    kernel_init_stack_t f = {
        .r12 = 0,
        .r13 = 0,
        .r14 = 0,
        .r15 = 0,
        .rbp = 0,
        .rbx = 0,
        .trampoline = (void*) kernel_thread_trampoline,
        .fn = entry_fn,
        .exit = thread_exit,
    };

    uintptr_t sp_top = ((uintptr_t) stack + K_THREAD_STACK_SIZE) & ~0xFULL;
    uintptr_t sp = sp_top - sizeof(f);
    memcpy((void*) sp, &f, sizeof(f));

    t->rsp = sp;
    t->tid = next_tid++;
    t->name = name;
    t->status = STATUS_READY;
    t->cpu_id = CPU_CURRENT->seq_id;

    t->kstack_base = stack;
    t->kstack_size = K_THREAD_STACK_SIZE;

    t->sched_list_node = (list_node_t) { 0 };
    t->reap_list_node = (list_node_t) { 0 };
    t->sleep_timer = timer_create((timer_fn_t) sched_wake_thread, t);

    return t;
}
