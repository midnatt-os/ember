#pragma once

//#include "fs/fd.h"
#include "fs/fd.h"
#include "fs/vfs.h"
#include "lib/list.h"
#include "memory/vm.h"

#include <stdint.h>


typedef struct {
    uint64_t pid;
    char name[32];
    VmAddressSpace* as;

    FDTable fds;
    VNode* cwd;

    List threads;
    Mutex lock;
} Process;

Process* process_create(const char* name, VmAddressSpace* as);
