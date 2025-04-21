#include "cpu/cpu.h"

#include "stdint.h"


[[noreturn]] void cpu_halt() {
    while (true) {
        __builtin_ia32_pause();
        asm volatile("hlt");
    }
}

void cpu_relax() {
    asm volatile("pause");
}

void cpu_int_mask() {
    asm volatile("cli");
}

void cpu_int_unmask() {
    asm volatile("sti");
}

bool cpu_int_get_state() {
    uint64_t flags;
    asm volatile ("pushfq; popq %0" : "=r"(flags));
    return (flags & (1 << 9)) != 0;
}
