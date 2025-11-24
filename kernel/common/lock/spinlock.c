#include "common/lock/spinlock.h"

#include "common/asm.h"
#include "common/assert.h"

#define DEADLOCK_COUNT 100'000'000


static inline void spinlock_unlock_raw(spinlock_t* lock) {
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}

static void spinlock_lock_raw(spinlock_t* lock) {
    uint64_t dead = 0;
    while (true) {
        if (!__atomic_test_and_set(lock, __ATOMIC_ACQUIRE))
            return;

        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED)) {
            relax();
            ASSERT(dead++ != DEADLOCK_COUNT);
        }
    }
}

bool spinlock_lock(spinlock_t* lock) {
    bool prev = int_mask();
    spinlock_lock_raw(lock);
    return prev;
}

void spinlock_unlock(spinlock_t* lock, bool prev) {
    spinlock_unlock_raw(lock);
    int_restore(prev);
}
