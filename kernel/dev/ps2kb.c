#include "dev/ps2kb.h"
#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/ioapic.h"
#include "cpu/lapic.h"
#include "cpu/port.h"
#include "lib/ringbuf.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "uacpi/namespace.h"
#include "uacpi/resources.h"
#include "uacpi/utilities.h"
#include <stdint.h>

#define PS2_DATA_PORT   0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT    0x64

#define PS2_CMD_DISABLE_PORT1 0xAD
#define PS2_CMD_ENABLE_PORT1  0xAE
#define PS2_CMD_READ_CONFIG   0x20
#define PS2_CMD_WRITE_CONFIG  0x60

#define PS2_CONFIG_IRQ1_ENABLE (1 << 0)
#define PS2_CONFIG_TRANSLATE   (1 << 6)

#define PS2_CMD_SCANCODE 0xF0
#define PS2_ACK          0xFA

#define PS2_CMD_RESET   0xFF
#define PS2_SELFTEST_OK 0xAA



static const char* kbd_pnpids[] = {
    "PNP0300",
    "PNP0301",
    "PNP0302",
    "PNP0303",
    "PNP0304",
    "PNP0305",
    "PNP0306",
    "PNP0307",
    "PNP0308",
    "PNP0309",
    "PNP030A",
    "PNP030B",
    "PNP0320",
    "PNP0321",
    "PNP0322",
    "PNP0323",
    "PNP0324",
    "PNP0325",
    "PNP0326",
    "PNP0327",
    "PNP0340",
    "PNP0341",
    "PNP0342",
    "PNP0343",
    "PNP0343",
    "PNP0344",
    NULL
};

static uacpi_iteration_decision chk_for_ps2kb(void* found, [[maybe_unused]] uacpi_namespace_node* node, [[maybe_unused]] uint32_t unused) {
    *(bool*) found = true;
    return UACPI_ITERATION_DECISION_BREAK;
}

static void ps2_wait_input_clear() {
    while (port_inb(PS2_STATUS_PORT) & 0x02);
}

static void ps2_wait_output_full() {
    while (!(port_inb(PS2_STATUS_PORT) & 0x01));
}

static void ps2_flush_output() {
    while (port_inb(PS2_STATUS_PORT) & 0x01) port_inb(PS2_DATA_PORT);
}

static bool ps2kb_reset() {
    ps2_wait_input_clear();
    port_outb(PS2_DATA_PORT, PS2_CMD_RESET);

    ps2_wait_output_full();
    if (port_inb(PS2_DATA_PORT) != PS2_ACK)
        return false;

    ps2_wait_output_full();
    return port_inb(PS2_DATA_PORT) == PS2_SELFTEST_OK;
}

static bool ps2kb_set_scanset2() {
    ps2_wait_input_clear();
    port_outb(PS2_DATA_PORT, PS2_CMD_SCANCODE);

    ps2_wait_output_full();
    if (port_inb(PS2_DATA_PORT) != PS2_ACK)
        return false;

    ps2_wait_input_clear();
    port_outb(PS2_DATA_PORT, 0x02);

    ps2_wait_output_full();
    return port_inb(PS2_DATA_PORT) == PS2_ACK;
}

Thread* kb_worker_thread = nullptr;
RingBuffer* sc_buffer = nullptr;

static void keyboard_int_handler(InterruptFrame* _) {
    uint8_t sc = port_inb(PS2_DATA_PORT);

    if (ringbuf_push(sc_buffer, sc) && kb_worker_thread->status == STATUS_BLOCKED)
        sched_schedule_thread(kb_worker_thread);

    lapic_eoi();
}



typedef struct {
    uint8_t scancode;
    bool released;
    bool extended;
} KeyEvent;

typedef enum {
    SCAN_IDLE,
    SCAN_F0,       // waiting for release scancode
    SCAN_E0,       // waiting for extended scancode
    SCAN_E0_F0     // waiting for extended release
} ScanState;

typedef struct {
    ScanState state;
} ScanParser;

void scanparser_init(ScanParser* parser) {
    parser->state = SCAN_IDLE;
}

bool scanparser_feed(ScanParser* p, uint8_t byte, KeyEvent* out_event) {
    switch (p->state) {
        case SCAN_IDLE:
            if (byte == 0xF0) {
                p->state = SCAN_F0;
                return false;
            } else if (byte == 0xE0) {
                p->state = SCAN_E0;
                return false;
            } else {
                *out_event = (KeyEvent){ .scancode = byte, .released = false, .extended = false };
                return true;
            }

        case SCAN_F0:
            *out_event = (KeyEvent){ .scancode = byte, .released = true, .extended = false };
            p->state = SCAN_IDLE;
            return true;

        case SCAN_E0:
            if (byte == 0xF0) {
                p->state = SCAN_E0_F0;
                return false;
            } else {
                *out_event = (KeyEvent){ .scancode = byte, .released = false, .extended = true };
                p->state = SCAN_IDLE;
                return true;
            }

        case SCAN_E0_F0:
            *out_event = (KeyEvent){ .scancode = byte, .released = true, .extended = true };
            p->state = SCAN_IDLE;
            return true;

        default:
            p->state = SCAN_IDLE;
            return false;
    }
}

void kb_worker() {
    logln(LOG_INFO, "PS2KB", "Worker thread online");

    ScanParser parser;
    scanparser_init(&parser);

    while (true) {
        sched_yield(STATUS_BLOCKED);

        while (!ringbuf_empty(sc_buffer)) {
            uint8_t sc;
            ringbuf_pop(sc_buffer, &sc);

            KeyEvent event;
            if (scanparser_feed(&parser, sc, &event)) {
                // Full event received
                logln(LOG_WARN, "PS2KB", "Key %s 0x%02x %s",
                    event.extended ? "E0" : "  ",
                    event.scancode,
                    event.released ? "released" : "pressed");

                // TODO: map to characters, send to TTY, etc.
            }
        }
    }
}

static InterruptEntry kb_int_entry = { .type = INT_HANDLER_NORMAL, .normal_handler = keyboard_int_handler };

void ps2kb_init() {
    bool found_kb = false;
    uacpi_find_devices_at(uacpi_namespace_root(), kbd_pnpids, chk_for_ps2kb, &found_kb);

    if (!found_kb) {
        logln(LOG_WARN, "PS2KB", "Not found");
        return;
    }

    ps2_wait_input_clear();
    port_outb(PS2_CMD_PORT, PS2_CMD_DISABLE_PORT1);

    ps2_flush_output();

    ps2_wait_input_clear();
    port_outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);

    ps2_wait_output_full();
    uint8_t config = port_inb(PS2_DATA_PORT);

    config |= PS2_CONFIG_IRQ1_ENABLE;
    config &= ~PS2_CONFIG_TRANSLATE;

    ps2_wait_input_clear();
    port_outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
    ps2_wait_input_clear();
    port_outb(PS2_DATA_PORT, config);

    ps2_wait_input_clear();
    port_outb(PS2_CMD_PORT, PS2_CMD_ENABLE_PORT1);

    ps2_flush_output();

    if (!ps2kb_reset()) {
        logln(LOG_WARN, "PS2KB", "Keyboard reset failed");
        return;
    }

    if (!ps2kb_set_scanset2()) {
        logln(LOG_WARN, "PS2KB", "Failed to set scan set 2");
        return;
    }

    uint8_t vec = interrupts_request_vector(&kb_int_entry);
    ASSERT(vec != 0);

    ioapic_redirect_irq(1, vec, cpu_current()->lapic_id, true);



    kb_worker_thread = thread_kernel_create(kb_worker, "kb_worker");
    sched_schedule_thread(kb_worker_thread);

    sc_buffer = ringbuf_new(256);

    logln(LOG_INFO, "PS2KB", "Initialized");
}
