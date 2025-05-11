#include "cpu/lapic.h"

#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/port.h"
#include "cpu/registers.h"
#include "lib/math.h"
#include "memory/hhdm.h"
#include "memory/vm.h"
#include "sys/time.h"

#define BASE_MASK 0xFFFFFFFFFF000
#define LAPIC_ENABLE (1 << 8)
#define LAPIC_SPURIOUS_VECTOR 0xFF

#define REG_SPURIOUS 0xF0
#define REG_EOI 0xB0
#define REG_ICR0 0x300
#define REG_ICR1 0x310
#define REG_LVT_TIMER 0x320
#define REG_TIMER_DIV 0x3E0
#define REG_TIMER_INIT 0x380
#define REG_TIMER_CURRENT 0x390

#define TIMER_PERIODIC (1 << 17)



static void disable_pic() {
    port_outb(0x20, 0x11);
    port_outb(0xA0, 0x11);
    port_outb(0x21, 0x20);
    port_outb(0xA1, 0x28);
    port_outb(0x21, 0x02);
    port_outb(0xA1, 0x04);
    port_outb(0x21, 0x01);
    port_outb(0xA1, 0x01);
    port_outb(0x21, 0xFF);
    port_outb(0xA1, 0xFF);
}

static void lapic_write(uint32_t reg, uint32_t data) {
    *(volatile uint32_t*) HHDM((read_msr(MSR_APIC_BASE) & BASE_MASK) + reg) = data;
}

static uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*) HHDM((read_msr(MSR_APIC_BASE) & BASE_MASK) + reg);
}

static void lapic_spurious_handler(InterruptFrame* _) {
    lapic_write(REG_EOI, 0);
}

static uint64_t ns_to_ticks(uint64_t ns) {
    return CLAMP(cpu_current()->lapic_timer_freq * ns / 1'000'000'000, 1, UINT32_MAX);
}

void lapic_eoi() {
    lapic_write(REG_EOI, 0);
}

void lapic_ipi(uint32_t dest_lapic_id, uint8_t vector) {
    while (lapic_read(REG_ICR0) & (1 << 12));

    lapic_write(REG_ICR1, dest_lapic_id << 24);
    lapic_write(REG_ICR0, vector);
}

void lapic_timer_oneshot(uint64_t ns, uint8_t vector) {
    uint64_t ticks = ns_to_ticks(ns);
    lapic_write(REG_LVT_TIMER, vector);
    lapic_write(REG_TIMER_INIT, ticks);
}

void lapic_timer_periodic(uint64_t ns, uint8_t vector) {
    uint64_t ticks = ns_to_ticks(ns);
    lapic_write(REG_LVT_TIMER, vector | TIMER_PERIODIC);
    lapic_write(REG_TIMER_INIT, ticks);
}

void lapic_timer_init() {
    lapic_write(REG_TIMER_DIV, 0x3); // divisor 16
    lapic_write(REG_TIMER_INIT, UINT32_MAX);

    uint64_t start = time_current();

    while (time_current() - start < ms_to_ns(100));

    uint64_t elapsed = time_current() - start;
    uint64_t tick_delta = UINT32_MAX - lapic_read(REG_TIMER_CURRENT);

    cpu_current()->lapic_timer_freq = (tick_delta * 1'000'000'000) / elapsed;

    lapic_write(REG_TIMER_INIT, 0);
    logln(LOG_INFO, "LAPIC", "CPU%u timer freq: %u Hz", cpu_current()->lapic_id, cpu_current()->lapic_timer_freq);
}

void lapic_init() {
    if (cpu_is_bsp()) {
        disable_pic();
        ASSERT(vm_map_direct(&kernel_as, (void*) HHDM(read_msr(MSR_APIC_BASE) & BASE_MASK),PAGE_SIZE, read_msr(MSR_APIC_BASE) & BASE_MASK, VM_PROT_RW, VM_CACHING_UNCACHED, VM_FLAG_FIXED) != nullptr);
    }

    interrupts_set_handler(LAPIC_SPURIOUS_VECTOR, lapic_spurious_handler);
    lapic_write(REG_SPURIOUS, LAPIC_SPURIOUS_VECTOR | LAPIC_ENABLE);
}