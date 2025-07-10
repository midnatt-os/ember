#pragma once

#include "lib/ringbuf.h"
#include "sched/thread.h"

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
    RingBuffer* sc_buffer;
    RingBuffer* ke_buffer;
    Thread* waiting_thread;
    Thread* worker_thread;

    ScanState scan_state;
} PS2Keyboard;

void ps2kb_init();
