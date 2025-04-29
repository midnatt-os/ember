#include "sys/time.h"

#include "common/assert.h"


static TimeSource* time_source = nullptr;

void time_register_source(TimeSource* source) {
    time_source = source;
}

uint64_t time_current() {
    ASSERT(time_source != nullptr);
    return time_source->current();
}
