#include "common/lock/spinlock.h"

#include "stdint.h"
#include "common/panic.h"
#include "cpu/cpu.h"


void spinlock_acquire(Spinlock* slock) {
    volatile uint64_t deadlock_cnt = 0;

    while (true) {
        if (!__atomic_test_and_set(&slock->locked, __ATOMIC_ACQUIRE))
        {
            slock->prev_interrupt_state = cpu_int_get_state();
            cpu_int_mask();
            return;
        }

        while (__atomic_load_n(&slock->locked, __ATOMIC_RELAXED)) {
            cpu_relax();

            if (deadlock_cnt++ >= 100'000'000)
                panic("Deadlock occured. Return addr: %llx", __builtin_return_address(0));
        }
    }
}

void spinlock_release(Spinlock* slock) {
    bool prev_int_state = slock->prev_interrupt_state;

    __atomic_clear(&slock->locked, __ATOMIC_RELEASE);

    if (prev_int_state)
        cpu_int_unmask();
}

void spinlock_primitive_acquire(Spinlock* slock) {
    volatile uint64_t deadlock_cnt = 0;

    while (true) {
        if (!__atomic_test_and_set(&slock->locked, __ATOMIC_ACQUIRE))
            return;

        while (__atomic_load_n(&slock->locked, __ATOMIC_RELAXED)) {
            cpu_relax();

            if (deadlock_cnt++ >= 100'000'000)
                panic("Deadlock occure, return addr: %llx", __builtin_return_address(0));
        }
    }
}

void spinlock_primitive_release(Spinlock* slock) {
    __atomic_clear(&slock->locked, __ATOMIC_RELEASE);
}