#pragma once

#include "lib/rb.h"

#include <stdint.h>

typedef void (*timer_fn_t)(void*);

struct timer_queue;
typedef struct timer_queue timer_queue_t;

typedef struct {
    uint64_t expiration_time;
    timer_fn_t callback;
    void* data;
    bool armed;
    rb_node_t node;
} timer_t;

struct timer_queue {
    rb_tree_t queue;
    uint64_t next_deadline;
};

static inline timer_t timer_create(timer_fn_t callback, void* arg) {
    return (timer_t) {
        .callback = callback,
        .data = arg,
        .node = { 0 },
        .armed = false,
    };
}

void timer_in_abs(timer_t* timer, uint64_t expiration_time);
void timer_in(timer_t* timer, uint64_t delay);

void timer_mod_abs(timer_t* timer, uint64_t new_expiration_time);
void timer_mod(timer_t* timer, uint64_t delay);

void timer_cancel(timer_t* timer);

void timer_init_cpu();
