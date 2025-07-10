#pragma once

//#include "fs/fd.h"
#include "fs/fd.h"
#include "fs/vfs.h"
#include "lib/list.h"
#include "memory/vm.h"

#include <stdint.h>


typedef struct Process {
    uint64_t pid;
    char name[32];
    VmAddressSpace* as;

    FDTable fds;
    VNode* cwd;

    struct Process* parent;
    List children;
    ListNode child_node;

    List threads;
    Mutex lock;
} Process;

Process* process_create(const char* name, VmAddressSpace* as);
typedef struct SyscallSavedRegs SyscallSavedRegs;
Process* process_fork(Process* proc_to_fork, SyscallSavedRegs* regs);
