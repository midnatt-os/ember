#include "events/event.h"
#include "common/panic.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/lapic.h"
#include "lib/container.h"
#include "lib/list.h"
#include "lib/string.h"
#include "sys/time.h"
#include "common/assert.h"
#include "common/log.h"

#include <stdint.h>



uint8_t events_vector = 0;

static void timer_fire_at(uint64_t deadline) {
    lapic_timer_oneshot(deadline - time_current(), events_vector);
}

void event_add(Event* new_event) {
    bool prev = cpu_int_mask();

    List* events = &cpu_current()->events;

    ListNode* before = nullptr;

    // Find insertion point
    LIST_FOREACH(*events, n) {
        Event* e = CONTAINER_OF(n, Event, list_node);
        if (new_event->deadline < e->deadline) {
            before = n;
            break;
        }
    }

    // Insert in correct position
    if (before) {
        list_node_prepend(events, before, &new_event->list_node);
    } else {
        list_append(events, &new_event->list_node); // Append at end
    }

    // If new_event is now at the head, set timer
    if (events->head == &new_event->list_node) {
        if (new_event->deadline <= time_current())
            timer_fire_at(1); // Immediate
        else
            timer_fire_at(new_event->deadline);
    }

    cpu_int_restore(prev);
}

void event_cancel(Event *event) {
    bool prev = cpu_int_mask();

    List* events = &cpu_current()->events;

    ListNode* head = list_peek(events);
    if (head == nullptr) {
        cpu_int_restore(prev);
        return;
    }

    // Special case: event is at head
    if (head == &event->list_node) {
        list_delete(events, head);

        if (!list_is_empty(events)) {
            Event* new_head = CONTAINER_OF(list_peek(events), Event, list_node);
            timer_fire_at(new_head->deadline);
        }

        cpu_int_restore(prev);
        return;
    }

    // Search for event->list_node elsewhere in the list
    LIST_FOREACH(*events, n) {
        if (n == &event->list_node) {
            list_delete(events, n);
            break;
        }
    }

    cpu_int_restore(prev);
}

InterruptEntry event_handler = { .type = INT_HANDLER_NORMAL, .normal_handler = event_handle_next };

void event_handle_next([[maybe_unused]] InterruptFrame* ctx) {
    lapic_eoi();

    List* events = &cpu_current()->events;

    LIST_FOREACH(*events, n) {
        Event* e = CONTAINER_OF(n, Event, list_node);

        if (e->deadline > time_current()) break;

        list_delete(events, &e->list_node);
        e->callback(e->callback_arg);
    }

    ListNode* next = list_peek(events);
    if (next == nullptr) return;

    uint64_t deadline = LIST_ELEMENT_OF(next, Event, list_node)->deadline;
    timer_fire_at(deadline);
}

void event_init() {
    if (cpu_is_bsp()) {
        int16_t vec = interrupts_request_vector(&event_handler);
        ASSERT(vec != -1);
        events_vector = vec;
        logln(LOG_INFO, "EVENTS", "Events system initialized");
    }

    cpu_current()->events = LIST_NEW;
}
