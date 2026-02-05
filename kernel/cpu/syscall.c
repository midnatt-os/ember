#include "cpu/syscall.h"

#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/msr.h"

#define EFER_SCE (1ULL << 0)

extern void syscall_entry();

void syscall_init() {
    msr_write(MSR_EFER, msr_read(MSR_EFER) | EFER_SCE);
    msr_write(MSR_STAR, ((uint64_t) GDT_SEL_CODE_CPL0 << 32) | ((uint64_t) (GDT_SEL_DATA_CPL3 - 8) << 48));
    msr_write(MSR_LSTAR, (uint64_t) syscall_entry);
    msr_write(MSR_SFMASK, msr_read(MSR_SFMASK) | (1 << 9));
    msr_write(MSR_GS_KERNEL_BASE, (uint64_t) CPU_CURRENT);
}
