#include "common/asm.h"
#include "common/assert.h"
#include "common/limine_requests.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "common/panic.h"
#include "common/stack_trace.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/interrupts.h"
#include "cpu/lapic.h"
#include "cpu/msr.h"
#include "cpu/pat.h"
#include "cpu/tsc.h"
#include "dev/hpet.h"
#include "lib/container.h"
#include "lib/elf.h"
#include "lib/mem.h"
#include "lib/rb.h"
#include "limine.h"
#include "mem/heap.h"
#include "mem/hhdm.h"
#include "mem/page.h"
#include "mem/pmm.h"
#include "mem/ptm.h"
#include "mem/slab.h"
#include "mem/vm.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "sys/acpi.h"
#include "sys/modules.h"
#include "sys/time.h"
#include "sys/timers.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

cpu_t* cpus = nullptr;
volatile uint64_t cpu_online_count = 0;

uint64_t current_ap_id = 1;
static bool aps_release_barrier = false;

static void wait_for_aps_release() {
    while (!atomic_load_explicit(&aps_release_barrier, memory_order_acquire))
        relax();
}

void ap_init([[maybe_unused]] struct limine_mp_info* cpu_info) {
    gdt_init();
    interrupts_init();
    pat_enable();
    vm_load_as(&global_as);
    vm_ap_init();

    cpu_t* cpu = &cpus[cpu_info->extra_argument];
    msr_write(MSR_GS_BASE, (uint64_t) cpu);

    *cpu = (cpu_t) {
        .self = cpu,
        .seq_id = cpu_info->extra_argument,
        .lapic_id = cpu_info->lapic_id,
    };

    logln(LOG_INFO, "SMP", "CPU%lu online", cpu->seq_id);

    lapic_init();

    timer_init_cpu();
    __atomic_fetch_add(&cpu_online_count, 1, __ATOMIC_RELEASE);

    wait_for_aps_release();
    sched_init_cpu();

    while (true)
        halt();
}

[[noreturn]] void init() {
    log_init();
    load_kernel_symbols();

    gdt_init();
    interrupts_init();

    pmm_init();
    pat_enable();
    vm_init();
    slab_init();
    heap_init();

    acpi_early_init();

    hpet_init();
    tsc_init();

    struct limine_mp_response* mp = mp_request.response;

    cpus = heap_alloc(sizeof(cpu_t) * mp->cpu_count);
    memclear(cpus, sizeof(cpu_t) * mp->cpu_count);

    for (size_t i = 0; i < mp->cpu_count; i++) {
        struct limine_mp_info* cpu_info = mp->cpus[i];
        if (cpu_info->lapic_id == mp->bsp_lapic_id) {
            msr_write(MSR_GS_BASE, (uint64_t) &cpus[i]);
            cpus[i] = (cpu_t) {
                .self = &cpus[i],
                .seq_id = i,
                .lapic_id = cpu_info->lapic_id,
            };

            continue;
        }

        cpu_info->extra_argument = i;
        cpu_info->goto_address = ap_init;
    }

    lapic_bsp_init();

    timer_init_cpu();
    cpu_online_count = 1;

    thread_cache = slab_create_cache("thread", sizeof(thread_t), PAGE_SIZE);

    atomic_store_explicit(&aps_release_barrier, true, memory_order_release);
    sched_init_cpu();

    while (true)
        halt();
}
