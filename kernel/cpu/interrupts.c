#include "cpu/interrupts.h"

#include "common/asm.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "common/panic.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "mem/vm.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "stdatomic.h"

#include <stddef.h>
#include <stdint.h>

#define IDT_SIZE 256
#define EXCEPTIONS_END_OFFSET 31

typedef struct [[gnu::packed]] {
    uint16_t limit;
    uint64_t base;
} idtr_t;

typedef struct {
    uint16_t low_offset;
    uint16_t segment_selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t middle_offset;
    uint32_t high_offset;
    uint32_t rsv0;
} idt_entry_t;

static idt_entry_t idt_entries[IDT_SIZE];
extern uint64_t isr_stubs[IDT_SIZE];

static spinlock_t handler_lock = SPINLOCK_NEW;
static interrupt_handler_t int_handlers[IDT_SIZE];

void common_int_handler(interrupt_frame_t* frame) {
    vm_tlb_maybe_flush_local();

    bool prev = spinlock_lock(&handler_lock);
    interrupt_handler_t handler = int_handlers[frame->vector];
    spinlock_unlock(&handler_lock, prev);

    if (handler == nullptr)
        logln(LOG_WARN, "INT", "interrupt raised but no handler present (vector: %lu)", frame->vector); // panic("interrupt raised but no handler present (vector: %lu)", frame->vector);
    else
        handler(frame);

    if (CPU_CURRENT->scheduler.need_resched) {
        CPU_CURRENT->scheduler.need_resched = false;
        sched_yield(STATUS_READY);
    }
}

void interrupts_set_handler(uint8_t vec, interrupt_handler_t handler) {
    bool prev = spinlock_lock(&handler_lock);
    int_handlers[vec] = handler;
    spinlock_unlock(&handler_lock, prev);
}

int16_t interrupts_request_vector(interrupt_handler_t handler) {
    bool prev = spinlock_lock(&handler_lock);

    for (size_t i = EXCEPTIONS_END_OFFSET; i < IDT_SIZE; i++) {
        if (int_handlers[i] != nullptr)
            continue;

        int_handlers[i] = handler;
        spinlock_unlock(&handler_lock, prev);
        return i;
    }

    spinlock_unlock(&handler_lock, prev);
    return -1;
}

void interrupts_load_idt() {
    idtr_t idtr = { .base = (uint64_t) &idt_entries, .limit = sizeof(idt_entries) - 1 };
    asm volatile("lidt %0" : : "m"(idtr));
}

void ss_handler(interrupt_frame_t* frame) {
    panic("-- STACK-SEGMENT FAULT --\nrip=%#p\nrsp=%#p", frame->rip, frame->rsp);
}

void gpf_handler(interrupt_frame_t* frame) {
    log_raw(
        "rax=%#lx rcx=%#lx rdx=%#lx rsi=%#lx rdi=%#lx rbx=%#lx rbp=%#lx r8=%#lx r9=%#lx r10=%#lx r11=%#lx r12=%#lx r13=%#lx r14=%#lx r15=%#lx\n",
        frame->rax,
        frame->rcx,
        frame->rdx,
        frame->rsi,
        frame->rdi,
        frame->rbx,
        frame->rbp,
        frame->r8,
        frame->r9,
        frame->r10,
        frame->r11,
        frame->r12,
        frame->r13,
        frame->r14,
        frame->r15
    );
    panic("-- GENERAL PROTECTION FAULT --\nrip=%#p\nrsp=%#p", frame->rip, frame->rsp);
}

void ud_handler(interrupt_frame_t* frame) {
    log_raw(
        "rax=%#lx rcx=%#lx rdx=%#lx rsi=%#lx rdi=%#lx rbx=%#lx rbp=%#lx\n" "r8 =%#lx r9 =%#lx r10=%#lx r11=%#lx r12=%#lx r13=%#lx r14=%#lx r15=%#lx\n",
        frame->rax,
        frame->rcx,
        frame->rdx,
        frame->rsi,
        frame->rdi,
        frame->rbx,
        frame->rbp,
        frame->r8,
        frame->r9,
        frame->r10,
        frame->r11,
        frame->r12,
        frame->r13,
        frame->r14,
        frame->r15
    );
    panic("-- INVALID OPCODE (#UD) --\nrip=%#p\nrsp=%#p", frame->rip, frame->rsp);
}

static inline char flag(uint64_t err, uint64_t bit, char c) {
    return (err & bit) ? c : '-';
}

void pf_handler(interrupt_frame_t* frame) {
    uint64_t err = frame->err_code;
    char flags[9] = {
        flag(err, 1u << 0, 'P'),
        flag(err, 1u << 1, 'W'),
        flag(err, 1u << 2, 'U'),
        flag(err, 1u << 3, 'R'),
        flag(err, 1u << 4, 'I'),
        flag(err, 1u << 5, 'K'), // PK
        flag(err, 1u << 6, 'S'), // SS
        flag(err, 1u << 7, 'X'), // SGX
        '\0',
    };

    panic("-- PAGE FAULT --\ncr2=%#p ERR=%#lx [%s]\nrsp=%#p\nrip=%#p\n", cr2_read(), err, flags, frame->rsp, frame->rip);
}

extern uint64_t panic_ack_count;

void panic_ipi_handler(interrupt_frame_t* _) {
    atomic_fetch_add_explicit(&panic_ack_count, 1, memory_order_release);

    int_mask();
    while (true)
        halt();
}

void interrupts_init() {
    for (size_t i = 0; i < IDT_SIZE; i++) {
        idt_entries[i] = (idt_entry_t) {
            .low_offset = (uint16_t) isr_stubs[i],
            .middle_offset = (uint16_t) (isr_stubs[i] >> 16),
            .high_offset = (uint32_t) (isr_stubs[i] >> 32),
            .segment_selector = GDT_SEL_CODE_CPL0,
            .flags = 0x8E,
            .ist = 0,
            .rsv0 = 0,
        };
    }

    interrupts_load_idt();

    int_handlers[0x2] = panic_ipi_handler;
    int_handlers[0x06] = ud_handler;
    int_handlers[0xC] = ss_handler;
    int_handlers[0xD] = gpf_handler;
    int_handlers[0xE] = pf_handler;

    int_unmask();
}
