#include "cpu/tsc.h"

#include "common/asm.h"
#include "common/log.h"
#include "dev/hpet.h"
#include "sys/time.h"

#define FS_PER_NS 1'000'000


static uint64_t period = 0;

uint64_t tsc_time() {
    return ((__uint128_t) __rdtsc() * period) / FS_PER_NS;
}

void tsc_init() {
    // TODO: assumes tsc exists and is invariant
    uint64_t t0 = __rdtsc();

    uint64_t end = hpet_time() + ms_to_ns(100);
    while (hpet_time() < end)
        relax();

    uint64_t t1 = __rdtsc();

    period = ((__uint128_t) ms_to_ns(100) * FS_PER_NS) / (t1 - t0);

    // logs now get a timestamp from the tsc
    extern uint64_t (*log_get_time)();
    log_get_time = tsc_time;

    uint64_t mhz = (1000000000lu + period / 2) / period;
    logln(LOG_INFO, "TSC", "freq: %llu MHz", (unsigned long long) mhz);
}
