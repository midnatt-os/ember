#pragma once

#include <stdint.h>

#define CLOCK_REALTIME            0
#define CLOCK_MONOTONIC           1
//#define CLOCK_PROCESS_CPUTIME_ID  2
//#define CLOCK_THREAD_CPUTIME_ID   3

typedef struct {
    char* name;
    uint64_t (*current)();
} TimeSource;

typedef struct {
	uint64_t tv_sec;
	long tv_nsec;
} TimeSpec;

static inline uint64_t ms_to_ns(uint64_t ms) { return ms * 1000000; }
static inline uint64_t s_to_ns (uint64_t s)  { return s * 1000000000; }
static inline uint64_t ns_to_ms(uint64_t ns) { return ns / 1000000; }
static inline uint64_t ns_to_s (uint64_t ns) { return ns / 1000000000; }

static inline void ns_to_timespec(uint64_t ns, TimeSpec* ts) {
    ts->tv_sec  = ns / 1000000000ULL;
    ts->tv_nsec = ns % 1000000000ULL;
}

void time_register_source(TimeSource* source);

uint64_t time_current();
void time_init();
