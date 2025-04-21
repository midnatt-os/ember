#pragma once

[[noreturn]] void cpu_halt();
void cpu_relax();
void cpu_int_mask();
void cpu_int_unmask();
bool cpu_int_get_state();

