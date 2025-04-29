#pragma once

#include <stdint.h>

typedef struct {
    char* name;
    uint64_t resolution;
    uint64_t (*current)();
} TimeSource;

static inline uint64_t ms_to_ns(uint64_t ms) { return ms * 1000000; }
static inline uint64_t s_to_ns (uint64_t s)  { return s * 1000000000; }
static inline uint64_t ns_to_ms(uint64_t ns) { return ns / 1000000; }
static inline uint64_t ns_to_s (uint64_t ns) { return ns / 1000000000; }

void time_register_source(TimeSource* source);

uint64_t time_current();
