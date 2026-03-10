#pragma once

#include <stdint.h>

typedef struct {
    int64_t s;
    int64_t ns;
} timespec_t;

static inline int timespec_is_valid(const timespec_t* ts) {
    if (!ts)
        return 1;
    if (ts->s < 0)
        return 0;
    if (ts->ns < 0 || ts->ns >= 1000000000LL)
        return 0;
    return 1;
}

static inline int timespec_is_zero(const timespec_t* ts) {
    return ts && ts->s == 0 && ts->ns == 0;
}

static inline uint64_t timespec_to_ms(timespec_t* ts) {
    return ((int64_t) ts->s * 1000) + (ts->ns / 1000000);
}

static inline uint64_t timespec_to_ns(const timespec_t* ts) {
    const uint64_t BILLION = 1000000000ULL;
    return ((uint64_t) ts->s * BILLION) + (uint64_t) ts->ns;
}

static inline uint64_t ms_to_ns(uint64_t ms) {
    return ms * 1000000;
}
static inline uint64_t s_to_ns(uint64_t s) {
    return s * 1000000000;
}
static inline uint64_t ns_to_ms(uint64_t ns) {
    return ns / 1000000;
}
static inline uint64_t ns_to_s(uint64_t ns) {
    return ns / 1000000000;
}
