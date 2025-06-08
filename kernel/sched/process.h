#pragma once

//#include "fs/fd.h"
#include "fs/fd.h"
#include "lib/list.h"
#include "memory/vm.h"

#include <stdint.h>


typedef struct {
    uint64_t pid;
    char name[32];
    VmAddressSpace* as;

    FDTable fds;

    List threads;
} Process;

Process* process_create(const char* name, VmAddressSpace* as);
