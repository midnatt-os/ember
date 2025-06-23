#include "cpu/interrupts.h"

#include <stddef.h>

#include "common/log.h"
#include "cpu/registers.h"
#include "common/assert.h"
#include "common/panic.h"
#include "common/lock/spinlock.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "uacpi/types.h"

#define IDT_SIZE 256
#define EXCEPTIONS_END_OFFSET 31



typedef struct [[gnu::packed]] {
    uint16_t limit;
    uint64_t base;
} IDTR;

typedef struct [[gnu::packed]] {
    uint16_t low_offset;
    uint16_t segment_selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t middle_offset;
    uint32_t high_offset;
    uint32_t rsv0;
} IdtEntry;

Spinlock int_handlers_lock = SPINLOCK_NEW;

static IdtEntry idt_entries[IDT_SIZE];
static InterruptEntry* int_handlers[IDT_SIZE];

extern uint64_t isr_stubs[IDT_SIZE];

void common_int_handler(InterruptFrame* frame) {
    spinlock_acquire(&int_handlers_lock);
    InterruptEntry* entry = int_handlers[frame->int_number];
    spinlock_release(&int_handlers_lock);

    if (entry == nullptr)
        panic("no interrupt handler for int_number: %u", frame->int_number); // TODO: Handle properly

    switch (entry->type) {
        case INT_HANDLER_NORMAL: entry->normal_handler(frame); break;
        case INT_HANDLER_UACPI: entry->uacpi_handler(entry->arg); break;
        default: break;
    }
}

static void gp_handler(InterruptFrame* frame) {
    uint32_t err = frame->err_code;
    uint32_t e   =  (err >> 0) & 1;        /* E */
    uint32_t tbl =  (err >> 1) & 3;        /* Tbl */
    uint32_t idx =  (err >> 3) & 0x1FFF;   /* Index */

    static const char *tbl_name[4] = {
        "GDT", "IDT", "LDT", "IDT"
    };

    panic(
        "GENERAL PROTECTION FAULT\n"
        "   External (E)?    : %d\n"
        "   Table referenced : %s\n"
        "   Selector index   : %u\n",
        e,
        tbl_name[tbl],
        idx
    );
}

static void pf_handler(InterruptFrame* frame) {
    uint32_t err = frame->err_code;

    panic(
        "PAGE FAULT\n"
        "   Faulting address (CR2) : %#p\n"
        "   Present violation?     : %d\n"
        "   Write access?          : %d\n"
        "   User-mode access?      : %d\n"
        "   Reserved-bit fault?    : %d\n"
        "   Instruction fetch?     : %d\n"
        "   Protection-key fault?  : %d\n"
        "   Shadow-stack fault?    : %d\n"
        "   SGX violation?         : %d\n",
        read_cr2(),
        (err >> 0) & 1,
        (err >> 1) & 1,
        (err >> 2) & 1,
        (err >> 3) & 1,
        (err >> 4) & 1,
        (err >> 5) & 1,
        (err >> 6) & 1,
        (err >> 7) & 1
    );
}

static void fill_idt() {
    for (size_t i = 0; i < IDT_SIZE; i++) {
        idt_entries[i] = (IdtEntry) {
            .low_offset = (uint16_t) isr_stubs[i],
            .middle_offset = (uint16_t) (isr_stubs[i] >> 16),
            .high_offset = (uint32_t) (isr_stubs[i] >> 32),
            .segment_selector = GDT_SELECTOR_CODE64_RING0,
            .flags = 0x8E,
            .ist = 0,
            .rsv0 = 0
        };
    }
}
void interrupts_load_idt() {
    IDTR idtr = { .base = (uint64_t) &idt_entries, .limit = sizeof(idt_entries) - 1 };
    asm volatile("lidt %0" : : "m" (idtr));
}

void interrupts_set_handler(uint8_t vector, InterruptEntry* entry) {
    spinlock_acquire(&int_handlers_lock);

    int_handlers[vector] = entry;

    spinlock_release(&int_handlers_lock);
}

int16_t interrupts_request_vector(InterruptEntry* entry) {
    spinlock_acquire(&int_handlers_lock);

    for (size_t i = EXCEPTIONS_END_OFFSET; i < IDT_SIZE; i++) {
        if (int_handlers[i] != nullptr)
            continue;

        int_handlers[i] = entry;
        spinlock_release(&int_handlers_lock);
        return i;
    }

    spinlock_release(&int_handlers_lock);
    return -1;
}

InterruptEntry gp_entry = { .type = INT_HANDLER_NORMAL, .normal_handler = gp_handler };
InterruptEntry pf_entry = { .type = INT_HANDLER_NORMAL, .normal_handler = pf_handler };

void interrupts_init() {
    for (size_t i = 0; i < IDT_SIZE; i++)
        int_handlers[i] = nullptr;

    int_handlers[0xD] = &gp_entry;
    int_handlers[0xE] = &pf_entry;

    fill_idt();
    interrupts_load_idt();

    cpu_int_unmask();

    logln(LOG_INFO, "INTERRUPTS", "Initialized");
}
