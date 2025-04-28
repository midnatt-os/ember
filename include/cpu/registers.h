#pragma once

#include <stdint.h>

static inline uint64_t read_cr2() {
    uint64_t value;
    asm volatile("mov %%cr2,%0":"=r"(value));
    return value;
}

static inline void write_cr3(uint64_t v) {
    asm volatile("mov %0,%%cr3"::"r"(v):"memory");
}