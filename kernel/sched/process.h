#pragma once

#include "lib/list.h"
#include "memory/vm.h"

#include <stdint.h>

typedef struct {
    uint64_t pid;
    char name[32];
    VmAddressSpace* as;

    List threads;
} Process;

Process* process_create(const char* name, VmAddressSpace* as);
