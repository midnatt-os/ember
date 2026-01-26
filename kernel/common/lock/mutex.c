#include "common/lock/mutex.h"

#include "common/lock/spinlock.h"
#include "lib/container.h"
#include "sched/sched.h"
#include "sched/thread.h"

#define SPIN_COUNT 10

static inline bool try_lock(mutex_t* mutex, bool weak) {
    mutex_state_t expected = MUTEX_STATE_UNLOCKED;
    return __atomic_compare_exchange_n(&mutex->state, &expected, MUTEX_STATE_LOCKED, weak, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void mutex_lock(mutex_t* mutex) {
retry:
    if (try_lock(mutex, true))
        return;

    for (int i = 0; i < SPIN_COUNT; i++) {
        if (try_lock(mutex, true))
            return;
        sched_yield(STATUS_READY);
    }

    bool prev = spinlock_lock(&mutex->lock);

    if (__atomic_load_n(&mutex->state, __ATOMIC_RELAXED) == MUTEX_STATE_UNLOCKED) {
        spinlock_unlock(&mutex->lock, prev);
        goto retry;
    }

    __atomic_store_n(&mutex->state, MUTEX_STATE_CONTESTED, __ATOMIC_RELAXED);

    thread_t* cur = sched_get_current_thread();
    list_append(&mutex->wait_queue, &cur->wq_node);

    spinlock_unlock(&mutex->lock, prev);

    sched_yield(STATUS_BLOCKED);

    goto retry;
}

void mutex_unlock(mutex_t* mutex) {
    mutex_state_t expected = MUTEX_STATE_LOCKED;
    if (__atomic_compare_exchange_n(&mutex->state, &expected, MUTEX_STATE_UNLOCKED, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
        return;
    }

    bool prev = spinlock_lock(&mutex->lock);

    if (mutex->wait_queue.count != 0) {
        thread_t* t = CONTAINER_OF(list_pop(&mutex->wait_queue), thread_t, wq_node);

        __atomic_store_n(&mutex->state, MUTEX_STATE_UNLOCKED, __ATOMIC_RELEASE);

        sched_schedule_thread(t);
    } else {
        __atomic_store_n(&mutex->state, MUTEX_STATE_UNLOCKED, __ATOMIC_RELEASE);
    }

    spinlock_unlock(&mutex->lock, prev);
}
