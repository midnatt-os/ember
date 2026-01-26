#pragma once

#include "common/lock/spinlock.h"
#include "lib/list.h"

#define MUTEX_NEW ((mutex_t) { .state = MUTEX_STATE_UNLOCKED, .lock = SPINLOCK_NEW, .wait_queue = LIST_NEW })

typedef enum {
    MUTEX_STATE_UNLOCKED,
    MUTEX_STATE_LOCKED,
    MUTEX_STATE_CONTESTED
} mutex_state_t;

typedef struct {
    spinlock_t lock;
    mutex_state_t state;
    list_t wait_queue;
} mutex_t;

void mutex_lock(mutex_t* mutex);
void mutex_unlock(mutex_t* mutex);
