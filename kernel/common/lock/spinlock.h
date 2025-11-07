#pragma once

#define SPINLOCK_NEW ((spinlock_t) { .locked = false })

typedef struct {
    bool locked;
} spinlock_t;

bool spinlock_lock(spinlock_t* lock);
void spinlock_unlock(spinlock_t* lock, bool prev);
