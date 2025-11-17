#include "sys/timers.h"

#include "common/asm.h"
#include "common/assert.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/lapic.h"
#include "cpu/tsc.h"
#include "lib/container.h"
#include "lib/rb.h"

#include <stdint.h>


uint8_t timer_vec = 0;

static rb_value_t timer_rb_value(const rb_node_t* n) {
    return CONTAINER_OF(n, timer_t, node)->expiration_time;
}

static void program_next_deadline(timer_queue_t* q, uint64_t now) {
    rb_node_t* min = rb_minimum(&q->queue, q->queue.root);
    if (min == q->queue.nil) {
        q->next_deadline = UINT64_MAX;
        lapic_timer_stop();
        return;
    }

    timer_t* t = CONTAINER_OF(min, timer_t, node);
    q->next_deadline = t->expiration_time;

    uint64_t due = (q->next_deadline > now) ? (q->next_deadline - now) : 0;

    lapic_timer_one_shot(due, timer_vec);
}

static void timer_process([[maybe_unused]] interrupt_frame_t* frame) {
    timer_queue_t* q = &CPU_CURRENT->timer_queue;
    uint64_t now = tsc_time();

    ASSERT(q->queue.nil != nullptr);
    ASSERT(q->queue.root != nullptr); // root should be nil or a real node, but not NULL

    while (true) {
        // Optional: fast-path check
        if (q->queue.root == q->queue.nil) {
            q->next_deadline = UINT64_MAX;
            break;
        }

        rb_node_t* min = rb_minimum(&q->queue, q->queue.root);
        if (min == q->queue.nil) { // or just rely on the root check above
            q->next_deadline = UINT64_MAX;
            break;
        }

        timer_t* t = CONTAINER_OF(min, timer_t, node);
        if (t->expiration_time > now) {
            q->next_deadline = t->expiration_time;
            break;
        }

        rb_delete(&q->queue, &t->node);
        t->node = (rb_node_t) { 0 };
        t->armed = false;

        t->callback(t->data);
        now = tsc_time();
    }

    program_next_deadline(q, now);
    lapic_eoi();
}

void timer_in_abs(timer_t* timer, uint64_t expiration_time) {
    bool prev = int_mask();
    timer_queue_t* q = &CPU_CURRENT->timer_queue;

    // timer must not already be armed (if it is, that's a logic bug)
    ASSERT(!timer->armed);

    rb_node_t* min = rb_minimum(&q->queue, q->queue.root);
    uint64_t old_head = (min == q->queue.nil) ? UINT64_MAX : CONTAINER_OF(min, timer_t, node)->expiration_time;

    timer->expiration_time = expiration_time;
    rb_insert(&q->queue, &timer->node);
    timer->armed = true;

    if (expiration_time < old_head)
        program_next_deadline(q, tsc_time());

    int_restore(prev);
}

void timer_in(timer_t* timer, uint64_t delay) {
    timer_in_abs(timer, tsc_time() + delay);
}

void timer_mod_abs(timer_t* timer, uint64_t new_expiration_time) {
    bool prev = int_mask();
    timer_queue_t* q = &CPU_CURRENT->timer_queue;
    uint64_t now = tsc_time();

    rb_node_t* min = rb_minimum(&q->queue, q->queue.root);
    timer_t* old_head = (min == q->queue.nil) ? nullptr : CONTAINER_OF(min, timer_t, node);
    bool was_head = old_head == timer;

    if (timer->armed) {
        rb_delete(&q->queue, &timer->node);
        // don't zero node, we immediately reinsert
    }

    timer->expiration_time = new_expiration_time;
    rb_insert(&q->queue, &timer->node);
    timer->armed = true;

    min = rb_minimum(&q->queue, q->queue.root);
    if (min != q->queue.nil) {
        timer_t* tmin = CONTAINER_OF(min, timer_t, node);
        if (was_head || tmin == timer)
            program_next_deadline(q, now);
    }

    int_restore(prev);
}

void timer_mod(timer_t* timer, uint64_t delay) {
    timer_mod_abs(timer, tsc_time() + delay);
}

void timer_cancel(timer_t* timer) {
    bool prev = int_mask();
    timer_queue_t* q = &CPU_CURRENT->timer_queue;

    if (timer->armed) {
        rb_node_t* min = rb_minimum(&q->queue, q->queue.root);
        uint64_t old_head = (min == q->queue.nil) ? UINT64_MAX : CONTAINER_OF(min, timer_t, node)->expiration_time;

        rb_delete(&q->queue, &timer->node);
        timer->node = (rb_node_t) { 0 };
        timer->armed = false;

        if (old_head == timer->expiration_time)
            program_next_deadline(q, tsc_time());
    }

    int_restore(prev);
}

void timer_init_cpu() {
    CPU_CURRENT->timer_queue = (timer_queue_t) {
        .next_deadline = UINT64_MAX,
    };
    rb_tree_init(&CPU_CURRENT->timer_queue.queue, timer_rb_value);

    if (timer_vec == 0)
        timer_vec = interrupts_request_vector(timer_process);
}
