#include "cpu/lapic.h"

#include "common/asm.h"
#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/msr.h"
#include "cpu/port.h"
#include "cpu/tsc.h"
#include "mem/mmio.h"
#include "mem/page.h"
#include "sys/time.h"

#include <stdint.h>

#define LAPIC_ENABLE (1 << 8)
#define LAPIC_SPURIOUS_VECTOR 0xFF
#define LAPIC_LVT_MASK (1 << 16)

enum {
    LAPIC_REG_ID = 0x20,
    LAPIC_REG_EOI = 0xB0,
    LAPIC_REG_SPURIOUS = 0xF0,
    LAPIC_REG_ICR_LO = 0x300,
    LAPIC_REG_ICR_LO_STATUS = (1 << 12),
    LAPIC_REG_ICR_HI = 0x310,
    LAPIC_LVT_TIMER = 0x320,
    LAPIC_LVT_THERMAL = 0x330,
    LAPIC_LVT_PERFORMANCE = 0x340,
    LAPIC_LVT_LINT0 = 0x350,
    LAPIC_LVT_LINT1 = 0x360,
    LAPIC_LVT_ERROR = 0x370,
    LAPIC_TIMER_INITIAL_COUNT = 0x380,
    LAPIC_TIMER_COUNT = 0x390,
    LAPIC_TIMER_DIVIDE = 0x3E0,
};


void* lapic_base = nullptr;

static void disable_pic() {
    port_write8(0x20, 0x11);
    port_write8(0xA0, 0x11);
    port_write8(0x21, 0x20);
    port_write8(0xA1, 0x28);
    port_write8(0x21, 0x02);
    port_write8(0xA1, 0x04);
    port_write8(0x21, 0x01);
    port_write8(0xA1, 0x01);
    port_write8(0x21, 0xFF);
    port_write8(0xA1, 0xFF);
}

static uint32_t read(uint32_t reg) {
    return mmio_read32(lapic_base + reg);
}

static void write(uint32_t reg, uint32_t data) {
    mmio_write32(lapic_base + reg, data);
}

static void spur_handler(interrupt_frame_t* _) {
    write(LAPIC_REG_EOI, 0);
}

static uint64_t timer_measure() {
    write(LAPIC_TIMER_INITIAL_COUNT, UINT32_MAX);

    uint64_t t_end = tsc_time() + ms_to_ns(100);
    while (tsc_time() < t_end)
        relax();

    return (UINT32_MAX - read(LAPIC_TIMER_COUNT) + 100 / 2) / 100;
}

/*void lapic_timer_oneshot(uint64_t ns, uint8_t vector) {
    uint64_t ticks = ns_to_ticks(ns);
    lapic_write(REG_LVT_TIMER, vector);
    lapic_write(REG_TIMER_INIT, ticks);
}*/


void lapic_init() {
    interrupts_set_handler(LAPIC_SPURIOUS_VECTOR, spur_handler);
    write(LAPIC_REG_SPURIOUS, LAPIC_SPURIOUS_VECTOR | LAPIC_ENABLE);

    write(LAPIC_LVT_LINT0, LAPIC_LVT_MASK);
    write(LAPIC_LVT_LINT1, LAPIC_LVT_MASK);
    write(LAPIC_LVT_ERROR, 0xFE);
    write(0x280, 0); // clear ESR
    write(0x80, 0); // TPR accept all priorities

    write(LAPIC_TIMER_DIVIDE, 0x3); // 16
    CPU_CURRENT.lapic_timer_freq = timer_measure();
    logln(LOG_INFO, "LAPIC", "(CPU%lu) Initialized, freq: %llu", CPU_CURRENT.seq_id, CPU_CURRENT.lapic_timer_freq);
}

void lapic_bsp_init() {
    lapic_base = mmio_map(msr_read(MSR_APIC_BASE) & 0xF'FFFF'FFFF'F000, PAGE_SIZE);
    ASSERT(lapic_base);
    disable_pic();

    lapic_init();
}
