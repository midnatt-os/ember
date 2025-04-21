#include "cpu/cpu.h"



[[noreturn]] void cpu_halt() {
    while (true) {
        __builtin_ia32_pause();
        asm volatile("hlt");
    }
}
