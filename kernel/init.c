#include <stdatomic.h>
#include <stdint.h>

#include "common/modules.h"
#include "events/event.h"
#include "lib/list.h"
#include "limine.h"
#include "nanoprintf.h"
#include "common/assert.h"
#include "common/log.h"
#include "common/tar.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/interrupts.h"
#include "cpu/lapic.h"
#include "cpu/registers.h"
#include "memory/heap.h"
#include "memory/pmm.h"
#include "memory/vm.h"
#include "sched/process.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "sys/boot.h"
#include "sys/stack_trace.h"
#include "dev/acpi.h"
#include "fs/tmpfs.h"
#include "fs/vfs.h"
#include "dev/pci.h"
#include "sys/time.h"
#include "lib/mem.h"


uintptr_t g_hhdm_offset;

_Atomic size_t next_cpu_slot = 1;

[[noreturn]] void ap_init(SmpInfo* cpu_info) {
    char tag[5];
    npf_snprintf(tag, sizeof(tag), "CPU%u", cpu_info->lapic_id);
    logln(LOG_INFO, tag, "Waking up");

    vm_load_address_space(&kernel_as);

    size_t slot = next_cpu_slot++;
    Cpu* cpu = &cpus[slot];

    *cpu = (Cpu) {
        .self = cpu,
        .seq_id = slot,
        .lapic_id = cpu_info->lapic_id,
        .lapic_timer_freq = 0
    };

    write_msr(MSR_GS_BASE, (uint64_t) cpu);

    gdt_init();
    interrupts_load_idt();
    cpu_int_unmask();

    lapic_init();
    lapic_timer_init();

    event_init();

    Tss* tss = kmalloc(sizeof(Tss));
    memclear(tss, sizeof(Tss));
    gdt_load_tss(tss);

    cpu_current()->tss = tss;

    sched_init();
    sched_start();

    while (true)
        cpu_halt();
}

[[noreturn]] void bsp_init(BootInfo* boot_info) {
    load_kernel_symbols(&boot_info->modules);

    g_hhdm_offset = boot_info->hhdm_offset;

    gdt_init();

    pmm_init(&boot_info->memmap);

    interrupts_init();

    vm_init(boot_info->kernel_addr, boot_info->memmap);

    heap_init();

    acpi_init(boot_info->rsdp_address);

    time_init();

    pci_enumerate();

    // Mount tmpfs at root
    logln(LOG_INFO, "VFS", "Mounting tmpfs at '/' (rootfs)");
    ASSERT(vfs_mount("/", &tmpfs_ops) == VFS_RES_OK);

    // Load initrd into tmpfs
    Module* initrd_module = find_module(&boot_info->modules, "initrd");
    populate_tmpfs_from_initrd(initrd_module);

    cpu_count = boot_info->smp->cpu_count;
    cpus = kmalloc(sizeof(Cpu) * cpu_count);

    for (size_t i = 0; i < cpu_count; i++) {
        SmpInfo* cpu_info = boot_info->smp->cpus[i];
        if (cpu_info->lapic_id != boot_info->smp->bsp_lapic_id)
            continue;

        Cpu* cpu = &cpus[0];

        *cpu = (Cpu) {
            .self = cpu,
            .seq_id = 0,
            .lapic_id = cpu_info->lapic_id,
            .lapic_timer_freq = 0
        };

        write_msr(MSR_GS_BASE, (uint64_t) cpu);
    }

    lapic_init();
    lapic_timer_init();

    for (size_t i = 0; i < cpu_count; i++) {
        SmpInfo* cpu_info = boot_info->smp->cpus[i];
        if (cpu_info->lapic_id == boot_info->smp->bsp_lapic_id)
            continue;

        atomic_store_explicit(&cpu_info->goto_address, ap_init, memory_order_release);
    }

    event_init();

    Tss* bsp_tss = kmalloc(sizeof(Tss));
    memclear(bsp_tss, sizeof(Tss));
    gdt_load_tss(bsp_tss);
    cpu_current()->tss = bsp_tss;

    sched_init();

    VmAddressSpace* init_proc_as = vm_create_address_space();

    Process* init_proc = process_create("init", init_proc_as);

    Thread* init_thread = thread_create_user(init_proc, "init_main", 0x69420);
    list_append(&cpu_current()->scheduler->ready_queue, &init_thread->sched_list_node);

    sched_start();

    while (true)
        cpu_halt();
}
