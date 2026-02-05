#pragma once

#include <stdint.h>

typedef enum {
    MSR_PAT = 0x277,
    MSR_APIC_BASE = 0x1B,
    MSR_GS_BASE = 0xC000'0101,
    MSR_GS_KERNEL_BASE = 0xC000'0102,
    MSR_FS_BASE = 0xC000'0100,
    MSR_EFER = 0xC0000080,
    MSR_STAR = 0xC0000081,
    MSR_LSTAR = 0xC0000082,
    MSR_SFMASK = 0xC0000084,
} msr_t;

static inline uint64_t msr_read(msr_t msr) {
    uint32_t low;
    uint32_t high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return low + ((uint64_t) high << 32);
}

static inline void msr_write(msr_t msr, uint64_t value) {
    asm volatile("wrmsr" : : "a"((uint32_t) value), "d"((uint32_t) (value >> 32)), "c"(msr));
}
