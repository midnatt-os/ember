#include "common/log.h"
#include "cpu/cpu.h"
#include "sys/boot.h"
#include "sys/stack_trace.h"



[[noreturn]] void kinit(BootInfo* boot_info) {
    load_kernel_symbols(&boot_info->modules);



    while (true)
        cpu_halt();
}
