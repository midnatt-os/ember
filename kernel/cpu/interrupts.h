#pragma once

#include "uacpi/types.h"
#include <stdint.h>

typedef struct [[gnu::packed]] {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t int_number;
    uint64_t err_code, rip, cs, rflags, rsp, ss;
} InterruptFrame;

typedef void (*InterruptHandler)(InterruptFrame* frame);

typedef enum {
    INT_HANDLER_NORMAL,
    INT_HANDLER_UACPI
} IntHandlerType;

typedef struct {
    IntHandlerType type;
    union {
        InterruptHandler normal_handler;
        uacpi_interrupt_handler uacpi_handler;
    };
    void* arg;
} InterruptEntry;



void interrupts_set_handler(uint8_t vector, InterruptEntry* entry);
int16_t interrupts_request_vector(InterruptEntry* entry);
void interrupts_load_idt();
void interrupts_init();
