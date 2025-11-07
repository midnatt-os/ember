#pragma once

#include <stdint.h>

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t err_code, rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

typedef void (*interrupt_handler_t)(interrupt_frame_t* frame);

void interrupts_set_handler(uint8_t vec, interrupt_handler_t handler);
int16_t interrupts_request_vector(interrupt_handler_t handler);
void interrupts_load_idt();
void interrupts_init();
