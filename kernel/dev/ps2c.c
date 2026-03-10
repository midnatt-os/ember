#include "ps2c.h"

#include "common/asm.h"
#include "common/log.h"
#include "common/panic.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/lapic.h"
#include "cpu/port.h"
#include "dev/ioapic.h"
#include "dev/ps2kb.h"
#include "lib/ringbuf.h"
#include "mem/heap.h"

#include <stddef.h>
#include <stdint.h>

// Controller registers
#define PS2_DATA 0x60
#define PS2_STS 0x64
#define PS2_CMD 0x64

// Status bits
#define ST_OBF (1u << 0) // output buffer full (can read 0x60)
#define ST_IBF (1u << 1) // input buffer full (cannot write yet)
#define ST_AUX (1u << 5) // 1 = byte from port2 (aux), 0 = port1

// Controller commands
#define CMD_READ_CFG 0x20
#define CMD_WRITE_CFG 0x60
#define CMD_SELFTEST 0xAA
#define CMD_TEST_P1 0xAB
#define CMD_TEST_P2 0xA9
#define CMD_DIS_P1 0xAD
#define CMD_EN_P1 0xAE
#define CMD_DIS_P2 0xA7
#define CMD_EN_P2 0xA8
#define CMD_WRITE_P2 0xD4

// Config byte bits
#define CFG_IRQ1 (1u << 0)
#define CFG_IRQ2 (1u << 1)
#define CFG_CLK1 (1u << 4) // 0 = enabled (clock running), 1 = disabled
#define CFG_CLK2 (1u << 5) // 0 = enabled, 1 = disabled / not present
#define CFG_XLAT (1u << 6)

// Device responses
#define PS2_ACK 0xFA
#define PS2_RESEND 0xFE

#define RB_SZ 512

static ringbuf_t kb_rx;
static ringbuf_t mouse_rx;

static bool wait_ibf_clear(uint32_t spins) {
    while (spins--) {
        if ((port_read8(PS2_STS) & ST_IBF) == 0)
            return true;

        relax();
    }

    return false;
}

static bool wait_obf_set(uint32_t spins) {
    while (spins--) {
        if (port_read8(PS2_STS) & ST_OBF)
            return true;

        relax();
    }

    return false;
}

static void flush_output(void) {
    for (int i = 0; i < 1024; i++) {
        uint8_t s = port_read8(PS2_STS);

        if (!(s & ST_OBF))
            break;

        port_read8(PS2_DATA);
    }
}

static void write_cmd(uint8_t c) {
    if (!wait_ibf_clear(200000))
        panic("ps2c: IBF stuck (cmd)");

    port_write8(PS2_CMD, c);
}

static void write_data(uint8_t d) {
    if (!wait_ibf_clear(200000))
        panic("ps2c: IBF stuck (data)");

    port_write8(PS2_DATA, d);
}

static uint8_t read_data_blocking(void) {
    if (!wait_obf_set(200000))
        panic("ps2c: OBF timeout (read)");

    return port_read8(PS2_DATA);
}

static uint8_t read_cfg(void) {
    write_cmd(CMD_READ_CFG);
    return read_data_blocking();
}

static void write_cfg(uint8_t cfg) {
    write_cmd(CMD_WRITE_CFG);
    write_data(cfg);
}

static inline void drain_to_queues(void) {
    bool kbd_pushed = false;

    while (true) {
        uint8_t s = port_read8(PS2_STS);
        if (!(s & ST_OBF))
            break;

        uint8_t b = port_read8(PS2_DATA);

        if (s & ST_AUX) {
            (void) ringbuf_push(&mouse_rx, b);
        } else {
            if (ringbuf_push(&kb_rx, b))
                kbd_pushed = true;
        }
    }

    if (kbd_pushed)
        ps2kb_notify_input();
}


static void ps2c_shared_isr(interrupt_frame_t* _) {
    drain_to_queues();
    lapic_eoi();
}

static bool poll_byte(ps2_port_t* src, uint8_t* out, uint32_t spins) {
    while (spins--) {
        uint8_t s = port_read8(PS2_STS);

        if (s & ST_OBF) {
            *out = port_read8(PS2_DATA);
            *src = (s & ST_AUX) ? PS2_MOUSE_PORT : PS2_KB_PORT;
            return true;
        }

        relax();
    }

    return false;
}

static bool expect_ack(ps2_port_t port) {
    for (int i = 0; i < 16; i++) {
        ps2_port_t src;
        uint8_t b;

        if (!poll_byte(&src, &b, 200000))
            return false;

        if (src != port)
            continue;

        if (b == PS2_ACK)
            return true;

        if (b == PS2_RESEND)
            return false;

        return false;
    }

    return false;
}

static bool init_device(ps2_port_t port, uint8_t id[2], uint8_t* idlen) {
    *idlen = 0;
    flush_output();

    // Reset (0xFF)
    if (!ps2c_send(port, 0xFF))
        return false;

    if (!expect_ack(port))
        return false;

    // Self-test pass (0xAA)
    ps2_port_t src;
    uint8_t b;

    if (!poll_byte(&src, &b, 400000) || src != port || b != 0xAA)
        return false;

    // Optional extra byte after reset (often 0x00). Read only if present quickly.
    poll_byte(&src, &b, 20000);

    // Disable scanning/reporting (0xF5)
    if (!ps2c_send(port, 0xF5))
        return false;

    if (!expect_ack(port))
        return false;

    // Identify (0xF2)
    if (!ps2c_send(port, 0xF2))
        return false;

    if (!expect_ack(port))
        return false;

    // At least 1 ID byte
    if (!poll_byte(&src, &b, 200000) || src != port)
        return false;

    id[0] = b;
    *idlen = 1;

    // Optional 2nd ID byte (common for keyboards)
    if (poll_byte(&src, &b, 20000) && src == port) {
        id[1] = b;
        *idlen = 2;
    }

    // Enable scanning/reporting (0xF4)
    if (!ps2c_send(port, 0xF4))
        return false;

    if (!expect_ack(port))
        return false;

    return true;
}

bool ps2c_pop_kbd(uint8_t* out_byte) {
    return ringbuf_pop(&kb_rx, out_byte);
}

bool ps2c_pop_aux(uint8_t* out_byte) {
    return ringbuf_pop(&mouse_rx, out_byte);
}

bool ps2c_send(ps2_port_t port, uint8_t byte) {
    if (port == PS2_MOUSE_PORT) {
        write_cmd(CMD_WRITE_P2);
    }

    if (!wait_ibf_clear(200000))
        return false;

    port_write8(PS2_DATA, byte);
    return true;
}

void ps2c_init() {
    uint8_t dest_lapic_id = CPU_CURRENT->lapic_id;

    uint8_t* kb_buf = heap_alloc(RB_SZ);
    uint8_t* ms_buf = heap_alloc(RB_SZ);

    ringbuf_init(&kb_rx, kb_buf, RB_SZ);
    ringbuf_init(&mouse_rx, ms_buf, RB_SZ);

    int16_t v1 = interrupts_request_vector(ps2c_shared_isr);
    int16_t v2 = interrupts_request_vector(ps2c_shared_isr);

    interrupts_set_handler((uint8_t) v1, ps2c_shared_isr);
    interrupts_set_handler((uint8_t) v2, ps2c_shared_isr);

    ioapic_route_isa_irq(1, (uint8_t) v1, dest_lapic_id, true);
    ioapic_route_isa_irq(12, (uint8_t) v2, dest_lapic_id, true);

    // 1) Disable both ports
    write_cmd(CMD_DIS_P1);
    write_cmd(CMD_DIS_P2);

    // 2) Flush output buffer
    flush_output();

    // 3) Read config, disable IRQ bits and translation during init
    uint8_t cfg = read_cfg();
    cfg &= ~(CFG_IRQ1 | CFG_IRQ2 | CFG_XLAT);
    write_cfg(cfg);

    // 4) Controller self-test
    write_cmd(CMD_SELFTEST);
    uint8_t st = read_data_blocking();

    if (st != 0x55)
        panic("ps2c: controller self-test failed");

    // Some controllers reset config; restore
    write_cfg(cfg);

    // 5) Detect dual-channel
    write_cmd(CMD_EN_P2);
    uint8_t cfg2 = read_cfg();
    bool dual = ((cfg2 & CFG_CLK2) == 0);
    if (dual)
        write_cmd(CMD_DIS_P2);

    // 6) Port interface tests
    write_cmd(CMD_TEST_P1);
    bool p1_ok = (read_data_blocking() == 0x00);

    bool p2_ok = false;
    if (dual) {
        write_cmd(CMD_TEST_P2);
        p2_ok = (read_data_blocking() == 0x00);
    }

    // 7) Initialize devices (polling, IRQs still masked/off)
    uint8_t kbd_id[2] = {0, 0}, aux_id[2] = {0, 0};
    uint8_t kbd_idlen = 0, aux_idlen = 0;

    if (p1_ok) {
        write_cmd(CMD_EN_P1);
        if (!init_device(PS2_KB_PORT, kbd_id, &kbd_idlen))
            p1_ok = false;
        write_cmd(CMD_DIS_P1);
    }

    if (p2_ok) {
        write_cmd(CMD_EN_P2);
        if (!init_device(PS2_MOUSE_PORT, aux_id, &aux_idlen))
            p2_ok = false;
        write_cmd(CMD_DIS_P2);
    }

    // 8) Enable controller IRQ bits only for working ports
    cfg = read_cfg();
    cfg &= ~(CFG_IRQ1 | CFG_IRQ2 | CFG_XLAT);

    if (p1_ok)
        cfg |= CFG_IRQ1;
    if (p2_ok)
        cfg |= CFG_IRQ2;

    write_cfg(cfg);

    // 9) Enable ports + unmask IOAPIC routes for working ports
    if (p1_ok) {
        write_cmd(CMD_EN_P1);
        ioapic_route_isa_irq(1, (uint8_t) v1, dest_lapic_id, false);
    }

    if (p2_ok) {
        write_cmd(CMD_EN_P2);
        ioapic_route_isa_irq(12, (uint8_t) v2, dest_lapic_id, false);
    }

    flush_output();

    if (p1_ok)
        ps2kb_init(); // assumes it consumes via ps2c_pop_kbd/ps2c_send

    // if (p2_ok)
    // TODO: ps2mouse_init();
}
