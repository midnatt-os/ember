#pragma once

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void relax() {
    __builtin_ia32_pause();
}

static inline void halt() {
    while (true) {
        __builtin_ia32_pause();
        asm volatile("hlt");
    }
}

static inline bool int_state() {
    uint64_t flags;
    asm volatile("pushfq; popq %0" : "=r"(flags));
    return (flags & (1 << 9)) != 0;
}

static inline bool int_mask() {
    bool prev = int_state();
    asm volatile("cli");
    return prev;
}

static inline void int_unmask() {
    asm volatile("sti");
}

static inline void int_restore(bool state) {
    bool current_state = int_state();
    if (current_state == state)
        return;

    if (state)
        int_unmask();
    else
        int_mask();
}

static inline void invlpg(uintptr_t addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline void cr3_write(uint64_t v) {
    asm volatile("mov %0,%%cr3" ::"r"(v) : "memory");
}

static inline uint64_t cr3_read() {
    uint64_t value;
    asm volatile("mov %%cr3,%0" : "=r"(value));
    return value;
}

static inline uint64_t cr2_read() {
    uint64_t value;
    asm volatile("mov %%cr2,%0" : "=r"(value));
    return value;
}
