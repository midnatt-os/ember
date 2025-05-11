#pragma once

#include <stdint.h>

typedef struct [[gnu::packed]] {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t int_number;
    uint64_t err_code, rip, cs, rflags, rsp, ss;
} InterruptFrame;

typedef void (*InterruptHandler)(InterruptFrame* frame);

void interrupts_set_handler(uint8_t vector, InterruptHandler handler);
int16_t interrupts_request_vector(InterruptHandler handler);
void interrupts_load_idt();
void interrupts_init();
