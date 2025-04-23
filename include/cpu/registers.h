#pragma once

#include <stdint.h>

static inline uint64_t read_cr2() {
    uint64_t value;
    asm volatile("mov %%cr2,%0":"=r"(value));
    return value;
}
