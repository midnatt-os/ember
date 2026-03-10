#include "sys/timers.h"

#include "common/asm.h"
#include "common/assert.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/lapic.h"
#include "cpu/tsc.h"
#include "lib/container.h"
#include "lib/rb.h"
#include "sys/time.h"

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

static void timer_process(interrupt_frame_t* _) {
    timer_queue_t* q = &CPU_CURRENT->timer_queue;
    uint64_t now = tsc_time();

    ASSERT(q->queue.nil != NULL);
    ASSERT(q->queue.root != NULL);

    if (q->queue.root == q->queue.nil) {
        q->next_deadline = UINT64_MAX;
        lapic_eoi();
        return;
    }

    while (true) {
        rb_node_t* min = rb_minimum(&q->queue, q->queue.root);
        if (min == q->queue.nil) {
            q->next_deadline = UINT64_MAX;
            break;
        }

        timer_t* t = CONTAINER_OF(min, timer_t, node);
        if (t->expiration_time > now) {
            q->next_deadline = t->expiration_time;
            break;
        }

        rb_delete(&q->queue, &t->node);
        t->armed = false;
        t->node = (rb_node_t) {0};

        t->callback(t->data);
        now = tsc_time();
    }

    program_next_deadline(q, now);

    lapic_eoi();
}


void timer_in_abs(timer_t* timer, uint64_t expiration_time) {
    ASSERT(expiration_time > tsc_time());
    // Acquire the spinlock for exclusive access to the timer queue
    spinlock_t* lock = &CPU_CURRENT->timer_queue.lock;
    bool prev_lock = spinlock_lock(lock);

    timer->expiration_time = expiration_time;
    timer->armed = true;

    rb_insert(&CPU_CURRENT->timer_queue.queue, &timer->node);

    if (expiration_time < CPU_CURRENT->timer_queue.next_deadline)
        CPU_CURRENT->timer_queue.next_deadline = expiration_time;

    program_next_deadline(&CPU_CURRENT->timer_queue, tsc_time());

    spinlock_unlock(lock, prev_lock);
}

void timer_in(timer_t* timer, uint64_t delay) {
    timer_in_abs(timer, tsc_time() + delay);
}

void timer_mod_abs(timer_t* timer, uint64_t new_expiration_time) {
    spinlock_t* lock = &CPU_CURRENT->timer_queue.lock;
    bool prev_lock = spinlock_lock(lock);

    if (timer->expiration_time == new_expiration_time) {
        spinlock_unlock(lock, prev_lock);
        return;
    }

    if (timer->armed) {
        rb_delete(&CPU_CURRENT->timer_queue.queue, &timer->node);
        timer->armed = false;
    }

    timer->expiration_time = new_expiration_time;

    rb_insert(&CPU_CURRENT->timer_queue.queue, &timer->node);

    timer->armed = true;

    if (new_expiration_time < CPU_CURRENT->timer_queue.next_deadline) {
        CPU_CURRENT->timer_queue.next_deadline = new_expiration_time;
    }

    program_next_deadline(&CPU_CURRENT->timer_queue, tsc_time());

    spinlock_unlock(lock, prev_lock);
}


void timer_mod(timer_t* timer, uint64_t delay) {
    timer_mod_abs(timer, tsc_time() + delay);
}

void timer_cancel(timer_t* timer) {
    spinlock_t* lock = &CPU_CURRENT->timer_queue.lock;
    bool prev_lock = spinlock_lock(lock);

    if (!timer->armed) {
        spinlock_unlock(lock, prev_lock);
        return;
    }

    rb_delete(&CPU_CURRENT->timer_queue.queue, &timer->node);
    timer->armed = false;

    if (CPU_CURRENT->timer_queue.queue.root == CPU_CURRENT->timer_queue.queue.nil)
        CPU_CURRENT->timer_queue.next_deadline = UINT64_MAX;
    else
        CPU_CURRENT->timer_queue.next_deadline = timer_rb_value(rb_minimum(&CPU_CURRENT->timer_queue.queue, CPU_CURRENT->timer_queue.queue.root));

    program_next_deadline(&CPU_CURRENT->timer_queue, tsc_time());

    spinlock_unlock(lock, prev_lock);
}

void timer_init_cpu() {
    CPU_CURRENT->timer_queue = (timer_queue_t) {
        .next_deadline = UINT64_MAX,
        .lock = SPINLOCK_NEW,
    };
    rb_tree_init(&CPU_CURRENT->timer_queue.queue, timer_rb_value);

    if (timer_vec == 0) {
        timer_vec = interrupts_request_vector(timer_process);
    }
}
