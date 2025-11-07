#include "common/log.h"
#include "mem/vm.h"

#include <stdint.h>

char* s = "IDK LOL SUS";

void init() {
    log_raw("Hello from test module\n");
}

void deinit() {
    log_raw("Goodbye from test module: %s\n", s);
}
