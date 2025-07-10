#include <stdatomic.h>
#include <stdint.h>

#include "abi/fcntl.h"
#include "abi/sysv/auxv.h"
#include "abi/sysv/elf.h"
#include "abi/sysv/sysv.h"
#include "common/modules.h"
#include "common/panic.h"
#include "common/types.h"
#include "cpu/fpu.h"
#include "cpu/ioapic.h"
#include "dev/tty.h"
#include "drivers/ps2kb.h"
#include "events/event.h"
#include "fs/devfs/devfs.h"
#include "fs/file.h"
#include "fs/vnode.h"
#include "lib/list.h"
#include "limine.h"
#include "memory/ptm.h"
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
#include "sys/framebuffer.h"
#include "sys/stack_trace.h"
#include "dev/acpi.h"
#include "fs/tmpfs.h"
#include "fs/vfs.h"
#include "dev/pci.h"
#include "sys/time.h"
#include "lib/mem.h"
#include "syscall/syscall.h"


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

    fpu_init_cpu();

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

    framebuffer_init(boot_info->fb);

    heap_init();

    fpu_init();
    fpu_init_cpu();

    acpi_early_init(boot_info->rsdp_address);

    time_init();

    pci_enumerate();

    // Mount tmpfs at root
    logln(LOG_INFO, "VFS", "Mounting tmpfs at '/' (rootfs)");
    ASSERT(vfs_mount("/", "tmpfs", &tmpfs_ops) == 0);

    // Load initrd into tmpfs
    Module* initrd_module = find_module(&boot_info->modules, "initrd");
    ASSERT(initrd_module != nullptr);
    populate_tmpfs_from_initrd(initrd_module);

    logln(LOG_INFO, "VFS", "Mounting devfs at '/dev'");
    VNode* _;
    ASSERT(vfs_create_dir(nullptr, "/", "dev", &_) == 0);
    ASSERT(vfs_mount("/dev", "devfs", &devfs_ops) == 0);

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

    ioapic_init();
    acpi_finalize_init();

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

    ps2kb_init();
    tty_init();

    VmAddressSpace* init_proc_as = vm_create_address_space();
    [[maybe_unused]] Process* init_proc = process_create("init", init_proc_as);

    VNode* tty_node;
    ASSERT(vfs_lookup(nullptr, "/dev/tty", &tty_node) == 0);
    File* tty = file_create(tty_node, O_RDWR);
    tty->is_tty = true;

    init_proc->fds.fds[0] = tty;
    init_proc->fds.fds[1] = tty;
    init_proc->fds.fds[2] = tty;

    ElfFile* init_elf;
    ASSERT(elf_read("usr/bin/init", &init_elf) == ELF_RESULT_OK);
    ASSERT(elf_load(init_elf, init_proc_as) == ELF_RESULT_OK);

    uintptr_t entry;
    char* interp_path;
    switch (elf_lookup_interpreter(init_elf, &interp_path)) {
        case ELF_RESULT_OK:
            ElfFile* interp_elf;
            ASSERT(elf_read(interp_path, &interp_elf) == ELF_RESULT_OK);
            ASSERT(elf_load(interp_elf, init_proc_as) == ELF_RESULT_OK);
            entry = interp_elf->entry_point;
            break;

        case ELF_RESULT_ERR_NOT_FOUND: entry = init_elf->entry_point; break;

        default: panic("Elf interpreter lookup on init failed.");
    }

    Auxv auxv = { .entry = init_elf->entry_point };

    uintptr_t phdr_table;
    if (elf_lookup_phdr_table(init_elf, &phdr_table) == ELF_RESULT_OK) {
        auxv.phdr = phdr_table;
        auxv.phnum = init_elf->phdrs.count;
        auxv.phent = init_elf->phdrs.size;
    }

    kfree(init_elf);

    char* argv[] = { "/usr/bin/init", nullptr };
    char* envp[] = { nullptr };

    #define U_STACK_SIZE 4096 * 8
    uintptr_t user_stack = sysv_setup_stack(init_proc_as, U_STACK_SIZE, argv, envp, &auxv);

    Thread* init_thread = thread_create_user(init_proc, "init_main", entry, user_stack);
    sched_schedule_thread(init_thread);

    syscall_init();

    sched_start();

    while (true)
        cpu_halt();
}
