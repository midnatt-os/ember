#pragma once

#include "lib/list.h"
#include "memory/vm.h"

#include <stdint.h>

typedef struct {
    uint64_t pid;
    VmAddressSpace* as;

    List threads;
} Process;
