#pragma once

#include "common/lock/mutex.h"
#include "lib/container.h"
#include "lib/list.h"
#include "sched/sched.h"

typedef struct {
    list_node_t link;
    thread_t* thread;
    bool queued;
} wait_node_t;

typedef struct {
    mutex_t lock;
    list_t waiters;
} wait_queue_t;

static inline void waitq_init(wait_queue_t* wq) {
    wq->lock = MUTEX_NEW;
    wq->waiters = LIST_NEW;
}

static inline void wait_node_init(wait_node_t* wn, thread_t* t) {
    wn->link.next = nullptr;
    wn->link.prev = nullptr;
    wn->thread = t;
    wn->queued = false;
}

static inline void waitq_add(wait_queue_t* wq, wait_node_t* wn) {
    mutex_lock(&wq->lock);
    if (!wn->queued) {
        wn->queued = true;
        list_append(&wq->waiters, &wn->link);
    }
    mutex_unlock(&wq->lock);
}

static inline void waitq_remove(wait_queue_t* wq, wait_node_t* wn) {
    mutex_lock(&wq->lock);
    if (wn->queued) {
        wn->queued = false;
        list_delete(&wq->waiters, &wn->link);
    }
    mutex_unlock(&wq->lock);
}

static inline void waitq_wake_all(wait_queue_t* wq) {
    for (;;) {
        mutex_lock(&wq->lock);
        list_node_t* n = list_pop(&wq->waiters);
        if (!n) {
            mutex_unlock(&wq->lock);
            break;
        }

        wait_node_t* wn = CONTAINER_OF(n, wait_node_t, link);
        wn->queued = false;
        mutex_unlock(&wq->lock);

        sched_wake_thread(wn->thread);
    }
}

static inline void waitq_wake_one(wait_queue_t* wq) {
    mutex_lock(&wq->lock);
    list_node_t* n = list_pop(&wq->waiters);
    if (!n) {
        mutex_unlock(&wq->lock);
        return;
    }

    wait_node_t* wn = CONTAINER_OF(n, wait_node_t, link);
    wn->queued = false;
    mutex_unlock(&wq->lock);

    sched_wake_thread(wn->thread);
}
