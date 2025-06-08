#include "common/lock/mutex.h"
#include "common/assert.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "lib/list.h"
#include "sched/sched.h"
#include "sched/thread.h"



static inline bool try_lock_fast(Mutex *mutex) {
    MutexState expected = MUTEX_UNLOCKED;
    return __atomic_compare_exchange_n(&mutex->state, &expected, MUTEX_LOCKED, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

void mutex_acquire(Mutex* mutex) {
    if (try_lock_fast(mutex))
        return;

    spinlock_acquire(&mutex->lock);

    MutexState prev = __atomic_exchange_n(&mutex->state, MUTEX_CONTESTED, __ATOMIC_ACQ_REL);

    if (prev == MUTEX_UNLOCKED) {
        __atomic_store_n(&mutex->state, MUTEX_LOCKED, __ATOMIC_RELEASE);
        spinlock_release(&mutex->lock);
        return;
    }

    Thread* self = sched_get_current_thread();
    list_append(&mutex->wait_queue, &self->wait_list_node);
    spinlock_release(&mutex->lock);

    sched_yield(STATUS_BLOCKED);
}

void mutex_release(Mutex* mutex) {
    MutexState expected = MUTEX_LOCKED;
    if (__atomic_compare_exchange_n(&mutex->state, &expected, MUTEX_UNLOCKED, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return;

    spinlock_acquire(&mutex->lock);

    ASSERT(mutex->state == MUTEX_CONTESTED);
    ASSERT(!list_is_empty(&mutex->wait_queue));

    Thread* next = LIST_ELEMENT_OF(list_pop(&mutex->wait_queue), Thread, wait_list_node);
    sched_schedule_thread(next);

    if(list_is_empty(&mutex->wait_queue))
        __atomic_store_n(&mutex->state, MUTEX_LOCKED, __ATOMIC_RELEASE);

    spinlock_release(&mutex->lock);
}
