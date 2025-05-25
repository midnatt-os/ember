#pragma once

#include "lib/list.h"
#include "cpu/interrupts.h"

#include <stdint.h>

typedef void (*EventCallback)([[maybe_unused]] void* arg);

typedef struct {
    uint64_t deadline;

    EventCallback callback;
    void* callback_arg;

    ListNode list_node;
} Event;

void event_add(Event* new_event);
void event_cancel(Event* event);
void event_handle_next([[maybe_unused]] InterruptFrame* ctx);
void event_init();
