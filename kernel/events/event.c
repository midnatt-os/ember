#include "events/event.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/lapic.h"
#include "lib/list.h"
#include "sys/time.h"
#include "common/assert.h"
#include "common/log.h"

#include <stdint.h>



uint8_t events_vector = 0;

static void timer_fire_at(uint64_t deadline) {
    lapic_timer_oneshot(deadline - time_current(), events_vector);
}

void event_add(Event* new_event) {
    if (list_is_empty(&cpu_current()->events)) {
        list_append(&cpu_current()->events, &new_event->list_node);
        timer_fire_at(new_event->deadline);
        return;
    }

    LIST_FOREACH(cpu_current()->events, n) {
        Event* event = LIST_ELEMENT_OF(n, Event, list_node);
        if (new_event->deadline < event->deadline) {
            list_node_prepend(&cpu_current()->events, &event->list_node, &new_event->list_node);

            if (list_peek(&cpu_current()->events) == &new_event->list_node)
                timer_fire_at(new_event->deadline);

            return;
        }
    }

    list_append(&cpu_current()->events, &new_event->list_node);
}

void event_cancel(Event *event) {
    if (list_peek(&cpu_current()->events) == &event->list_node) {
        list_delete(&cpu_current()->events, &event->list_node);

        if (!list_is_empty(&cpu_current()->events))
            timer_fire_at(LIST_ELEMENT_OF(list_peek(&cpu_current()->events), Event, list_node)->deadline);

        return;
    }

    list_delete(&cpu_current()->events, &event->list_node);
}

InterruptEntry event_handler = { .type = INT_HANDLER_NORMAL, .normal_handler = event_handle_next };

void event_handle_next([[maybe_unused]] InterruptFrame* ctx) {
    lapic_eoi();

    ListNode* head;
    while ((head = list_peek(&cpu_current()->events)) != nullptr) {
        Event* e = LIST_ELEMENT_OF(head, Event, list_node);

        uint64_t now = time_current();
        if (e->deadline > now) break;

        list_delete(&cpu_current()->events, head);
        e->callback(e->callback_arg);
    }

    ListNode* next = list_peek(&cpu_current()->events);
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
