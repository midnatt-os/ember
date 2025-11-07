#pragma once

#include <stdint.h>

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
