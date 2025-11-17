#include "thread.h"

#include "common/asm.h"
#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "lib/mem.h"
#include "mem/heap.h"
#include "mem/page.h"
#include "mem/slab.h"
#include "mem/vm.h"
#include "sched/sched.h"
#include "sys/timers.h"

#include <stdint.h>

typedef struct {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    void (*trampoline)();
    void (*fn)();
    void (*exit)();
} kernel_init_stack_t;


object_cache_t* thread_cache = nullptr; // TODO: .slab_caches linker section registration and init

uint64_t next_tid = 1;

void thread_exit() {
    int_mask();
    thread_t* self = CPU_CURRENT->scheduler.current_thread;
    self->status = STATUS_DONE;

    timer_cancel(&self->sleep_timer);

    // logln(LOG_DEBUG, "THREAD", "Thread %s (tid=%lu) exited", self->name, self->tid);

    sched_yield(STATUS_DONE);
    ASSERT_UNREACHABLE();
}

static void kernel_thread_trampoline([[maybe_unused]] thread_t* old_thread) {
    // sched_maybe_reschedule(old_thread);
    int_unmask();
}

thread_t* thread_create_kernel(char* name, void* entry_fn) {
    thread_t* t = slab_alloc(thread_cache);

    void* stack = vm_map_anon(&global_as, 0, 4 * PAGE_SIZE, 0, VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_ZERO);
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

    uintptr_t sp_top = ((uintptr_t) stack + 4 * PAGE_SIZE) & ~0xFULL;
    uintptr_t sp = sp_top - sizeof(f);
    memcpy((void*) sp, &f, sizeof(f));
    t->rsp = sp;
    t->tid = next_tid++;
    t->name = name;
    t->status = STATUS_READY;
    t->cpu_id = CPU_CURRENT->seq_id;

    t->kstack_base = stack;
    t->kstack_size = 4 * PAGE_SIZE;

    t->sched_list_node = (list_node_t) { 0 };
    t->sleep_timer = timer_create((timer_fn_t) sched_wake_thread, t);

    return t;
}
