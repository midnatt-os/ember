#include "cpu/cpu.h"


Cpu* cpus = nullptr;
size_t cpu_count = 0;

[[noreturn]] void cpu_halt() {
    while (true) {
        __builtin_ia32_pause();
        asm volatile("hlt");
    }
}

void cpu_relax() {
    asm volatile("pause");
}

bool cpu_int_get_state() {
    uint64_t flags;
    asm volatile ("pushfq; popq %0" : "=r"(flags));
    return (flags & (1 << 9)) != 0;
}

bool cpu_int_mask() {
    bool previous_state = cpu_int_get_state();
    asm volatile("cli");
    return previous_state;
}

void cpu_int_unmask() {
    asm volatile("sti");
}

void cpu_int_restore(bool state) {
    bool current_state = cpu_int_get_state();
    if (current_state == state) return;

    if (state)
        cpu_int_unmask();
    else
        cpu_int_mask();
}
