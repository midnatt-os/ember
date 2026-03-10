#pragma once

#include "common/lock/spinlock.h"
#include "fs/fd.h"
#include "fs/vfs.h"
#include "lib/list.h"
#include "mem/vm.h"

#include <stdint.h>

typedef enum {
    PROC_STATE_ALIVE,
    PROC_STATE_ZOMBIE,
} process_state_t;

typedef struct process process_t;

struct process {
    uint64_t pid;
    const char* name;

    vm_address_space_t* address_space;
    process_t* parent;
    list_t children;
    process_state_t state;

    fd_table_t* fd_table;
    vnode_t* cwd;
    int exit_code;

    vnode_t* ctty_vnode;

    list_t threads;
    spinlock_t lock;
};

process_t* proc_new(const char* name, process_t* parent);
void proc_load_init();
void proc_init();
