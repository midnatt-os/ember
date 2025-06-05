#pragma once

#include "common/lock/spinlock.h"
#include "lib/list.h"

#define MUTEX_NEW ((Mutex) { .lock = SPINLOCK_NEW, .state = MUTEX_UNLOCKED, .wait_queue = LIST_NEW })

typedef enum {
    MUTEX_UNLOCKED,
    MUTEX_LOCKED,
    MUTEX_CONTESTED,
} MutexState;

typedef struct {
    Spinlock lock;
    MutexState state;
    List wait_queue;
} Mutex;

void mutex_acquire(Mutex* mutex);
void mutex_release(Mutex* mutex);
