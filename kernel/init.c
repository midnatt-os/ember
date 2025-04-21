#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "memory/pmm.h"
#include "sys/boot.h"
#include "sys/stack_trace.h"





#define ACCESS_PRESENT (1 << 7)
#define ACCESS_DPL(DPL) (((DPL) & 0b11) << 5)
#define ACCESS_TYPE_TSS (9)
#define ACCESS_TYPE_CODE(CONFORM, READ) ((1 << 4) | (1 << 3) | ((CONFORM) << 2) | ((READ) << 1))
#define ACCESS_TYPE_DATA(DIRECTION, WRITE) ((1 << 4) | ((DIRECTION) << 2) | ((WRITE) << 1))
#define ACCESS_ACCESSED (1 << 0)

#define FLAG_GRANULARITY (1 << 7)
#define FLAG_DB (1 << 6)
#define FLAG_LONG (1 << 5)
#define FLAG_SYSTEM_AVL (1 << 4)



uintptr_t g_hhdm_offset;

[[noreturn]] void kinit(BootInfo* boot_info) {
    load_kernel_symbols(&boot_info->modules);

    g_hhdm_offset = boot_info->hhdm_offset;

    gdt_init();

    pmm_init(&boot_info->memmap);

    while (true)
        cpu_halt();
}
