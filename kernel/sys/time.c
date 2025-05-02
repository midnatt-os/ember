#include "sys/time.h"

#include "common/assert.h"
#include "common/log.h"
#include "cpu/tsc.h"
#include "dev/hpet.h"



extern TimeSource hpet_time_source;
extern TimeSource tsc_time_source;

TimeSource* time_source = nullptr;

void time_register_source(TimeSource* source) {
    logln(LOG_INFO, "TIME", "Registering time source %s", source->name);
    time_source = source;
}

uint64_t time_current() {
    ASSERT(time_source != nullptr);
    return time_source->current();
}

void time_init() {
    hpet_init();
    time_register_source(&hpet_time_source);
    tsc_init();
    time_register_source(&tsc_time_source);
}
