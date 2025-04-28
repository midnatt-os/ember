#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/interrupts.h"
#include "memory/heap.h"
#include "memory/pmm.h"
#include "memory/vm.h"
#include "sys/boot.h"
#include "sys/stack_trace.h"



uintptr_t g_hhdm_offset;

[[noreturn]] void kinit(BootInfo* boot_info) {
    load_kernel_symbols(&boot_info->modules);

    g_hhdm_offset = boot_info->hhdm_offset;

    gdt_init();

    pmm_init(&boot_info->memmap);

    interrupts_init();

    vm_init(boot_info->kernel_addr, boot_info->memmap);

    heap_init();

    while (true)
        cpu_halt();
}
