#include "drivers/ps2kb.h"
#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/ioapic.h"
#include "cpu/lapic.h"
#include "cpu/port.h"
#include "dev/keyboard.h"
#include "drivers/ps2kb.h"
#include "fs/devfs/devfs.h"
#include "fs/vnode.h"
#include "lib/ringbuf.h"
#include "memory/heap.h"
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

PS2Keyboard* kb_device = nullptr;

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

static void keyboard_int_handler(InterruptFrame* _) {
    uint8_t sc = port_inb(PS2_DATA_PORT);

    if (ringbuf_push(kb_device->sc_buffer, sc) && kb_device->worker_thread->status == STATUS_BLOCKED)
        sched_schedule_thread(kb_device->worker_thread);

    lapic_eoi();
}

bool scanparser_feed(uint8_t byte, KeyEvent* out_event) {
    switch (kb_device->scan_state) {
        case SCAN_IDLE:
            if (byte == 0xF0) {
                kb_device->scan_state = SCAN_F0;
                return false;
            } else if (byte == 0xE0) {
                kb_device->scan_state = SCAN_E0;
                return false;
            } else {
                *out_event = (KeyEvent) { .scancode = byte, .released = false, .extended = false };
                return true;
            }

        case SCAN_F0:
            *out_event = (KeyEvent) { .scancode = byte, .released = true, .extended = false };
            kb_device->scan_state = SCAN_IDLE;
            return true;

        case SCAN_E0:
            if (byte == 0xF0) {
                kb_device->scan_state = SCAN_E0_F0;
                return false;
            } else {
                *out_event = (KeyEvent) { .scancode = byte, .released = false, .extended = true };
                kb_device->scan_state = SCAN_IDLE;
                return true;
            }

        case SCAN_E0_F0:
            *out_event = (KeyEvent) { .scancode = byte, .released = true, .extended = true };
            kb_device->scan_state = SCAN_IDLE;
            return true;

        default:
            kb_device->scan_state = SCAN_IDLE;
            return false;
    }
}

void kb_worker() {
    logln(LOG_INFO, "PS2KB", "Worker thread online");

    while (true) {
        sched_yield(STATUS_BLOCKED);

        while (!ringbuf_empty(kb_device->sc_buffer)) {
            uint64_t sc;
            ringbuf_pop(kb_device->sc_buffer, &sc);

            KeyEvent event;
            if (!scanparser_feed(sc, &event)) continue;

            ringbuf_push(kb_device->ke_buffer, (uint64_t) &event);
            if (kb_device->waiting_thread != nullptr)
                sched_schedule_thread(kb_device->waiting_thread);
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

    kb_device = kmalloc(sizeof(PS2Keyboard));

    kb_device->sc_buffer = ringbuf_new(256);
    kb_device->ke_buffer = ringbuf_new(256);
    kb_device->worker_thread = thread_kernel_create(kb_worker, "kb_worker");
    kb_device->scan_state = SCAN_IDLE;

    sched_schedule_thread(kb_device->worker_thread);

    VNode* n;
    ASSERT(vfs_create_dir(nullptr, "/dev", "input", &n) == 0);

    devfs_register("/dev/input", "keyboard", VNODE_TYPE_CHAR_DEV, &kb_dev_ops, kb_device);

    logln(LOG_INFO, "PS2KB", "Initialized");
}
