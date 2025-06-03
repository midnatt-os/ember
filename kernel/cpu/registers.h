#pragma once

#include <stdint.h>

#define MSR_APIC_BASE 0x1B

#define MSR_GS_BASE 0xC000'0101
#define MSR_FS_BASE 0xC000'0100

#define MSR_EFER 0xC000'0080
#define MSR_STAR 0xC000'0081
#define MSR_LSTAR 0xC000'0082
#define MSR_CSTAR 0xC000'0083
#define MSR_SFMASK 0xC000'0084

static inline uint64_t read_msr(uint64_t msr) {
    uint32_t low;
    uint32_t high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return low + ((uint64_t) high << 32);
}

static inline void write_msr(uint64_t msr, uint64_t value) {
    asm volatile("wrmsr" : : "a"((uint32_t) value), "d"((uint32_t) (value >> 32)), "c"(msr));
}

static inline uint64_t read_cr2() {
    uint64_t value;
    asm volatile("mov %%cr2,%0":"=r"(value));
    return value;
}

static inline void write_cr3(uint64_t v) {
    asm volatile("mov %0,%%cr3"::"r"(v):"memory");
}

static inline uint64_t read_cr0(void) {
    uint64_t value;
    asm volatile (
        "mov %%cr0, %0"
        : "=r"(value)
        :
        :
    );
    return value;
}

static inline void write_cr0(uint64_t value) {
    asm volatile (
        "mov %0, %%cr0"
        :
        : "r"(value)
        : "memory"
    );
}

static inline uint64_t read_cr4(void) {
    uint64_t value;
    asm volatile (
        "mov %%cr4, %0"
        : "=r"(value)
        :
        :
    );
    return value;
}

static inline void write_cr4(uint64_t value) {
    asm volatile (
        "mov %0, %%cr4"
        :
        : "r"(value)
        : "memory"
    );
}
