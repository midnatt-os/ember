#pragma once

#include "common/assert.h"
#include <stdint.h>

typedef uint64_t ref_t;

static inline ref_t ref_count_inc(ref_t* ref_count) {
    return __atomic_add_fetch(ref_count, 1, __ATOMIC_RELAXED);
}

static inline ref_t ref_count_dec(ref_t* ref_count) {
    uint64_t res = __atomic_fetch_sub(ref_count, 1, __ATOMIC_RELAXED);
    ASSERT(res != 0);
    return res - 1;
}
