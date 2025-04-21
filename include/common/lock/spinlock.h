#pragma once

typedef struct {
    bool locked;
    bool prev_interrupt_state;
} Spinlock;

#define SPINLOCK_INIT ((Spinlock) {.locked = 0, .prev_interrupt_state = 0 });

void spinlock_acquire(Spinlock* slock);
void spinlock_release(Spinlock* slock);
void spinlock_primitive_acquire(Spinlock *slock);
void spinlock_primitive_release(Spinlock *slock);
