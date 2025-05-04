#include "cpu/lapic.h"

#include <stdint.h>

#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/registers.h"
#include "memory/hhdm.h"

#define BASE_MASK 0xFFFFFFFFFF000
#define LAPIC_ENABLE (1 << 8)
#define LAPIC_SPURIOUS_VECTOR 0xFF

#define REG_SPURIOUS 0xF0
#define REG_EOI 0xB0



static void lapic_write(uint32_t reg, uint32_t data) {
    *(volatile uint32_t*) HHDM((read_msr(MSR_APIC_BASE) & BASE_MASK) + reg) = data;
}

static uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*) HHDM((read_msr(MSR_APIC_BASE) & BASE_MASK) + reg);
}

static void lapic_spurious_handler(InterruptFrame* _) {
    lapic_write(REG_EOI, 0);
}

void lapic_eoi() {
    lapic_write(REG_EOI, 0);
}

void lapic_calibrate() {
    cpu_current()->lapic_timer_freq = 69420;
}

void lapic_init() {
    interrupts_set_handler(LAPIC_SPURIOUS_VECTOR, lapic_spurious_handler);
    lapic_write(REG_SPURIOUS, LAPIC_SPURIOUS_VECTOR | LAPIC_ENABLE);
}