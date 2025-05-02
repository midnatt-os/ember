#include "cpu/tsc.h"

#include <cpuid.h>
#include <stdint.h>

#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "sys/time.h"

#define TSC_SHIFT 32

/*static bool is_tsc_invariant() {
    uint32_t edx, unused;
    __get_cpuid(0x80000007, &unused, &unused, &unused, &edx);
    return (edx & (1 << 8)) != 0;
}*/

uint64_t tsc_freq; // Hz
uint64_t tsc_mult;

uint64_t tsc_current() {
    return ((unsigned __int128) __rdtsc() * tsc_mult) >> TSC_SHIFT;
}

TimeSource tsc_time_source = {
    .name = "TSC",
    .current = tsc_current
};


void tsc_init() {
    /* Can't check this until I start using kvm
    ASSERT(is_tsc_invariant());*/

    uint64_t t0 = __rdtsc();

    uint64_t target = time_current() + ms_to_ns(10);
    while (time_current() < target)
        cpu_relax();

    uint64_t tsc_delta = __rdtsc() - t0;
    tsc_freq = tsc_delta * 100;
    tsc_mult = ((uint64_t) 1000 * 1000 * 1000 << TSC_SHIFT) / tsc_freq;
}
