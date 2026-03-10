#include "sched/proc.h"

#include "common/assert.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "fs/fd.h"
#include "fs/file.h"
#include "fs/vfs.h"
#include "lib/elf.h"
#include "lib/list.h"
#include "mem/heap.h"
#include "mem/page.h"
#include "mem/slab.h"
#include "mem/vm.h"
#include "sched/sched.h"
#include "sched/thread.h"

#include <lib/string.h>
#include <stdatomic.h>
#include <stddef.h>

static object_cache_t* proc_cache = nullptr;

static uint64_t next_pid = 1;

uint64_t allocate_pid() {
    return atomic_fetch_add_explicit(&next_pid, 1, memory_order_relaxed);
}

void free_pid(uint64_t pid) {
    (void) pid;
}

process_t* proc_new(const char* name, process_t* parent) {
    ASSERT(name);
    process_t* p = slab_alloc(proc_cache);

    p->pid = allocate_pid();
    size_t len = strlen(name);
    char* name_buffer = heap_alloc(len + 1);
    strcpy(name_buffer, name);
    p->name = name_buffer;

    p->address_space = vm_new_address_space();
    p->parent = parent;
    p->children = LIST_NEW;
    p->state = PROC_STATE_ALIVE;

    p->threads = LIST_NEW;
    p->lock = SPINLOCK_NEW;

    p->fd_table = fd_table_alloc();

    if (parent) {
        // Inherit CWD from parent
        p->cwd = parent->cwd;

        /* TODO:
        if (p->cwd) {
            vfs_vnode_ref(p->cwd);
            }*/
    }

    return p;
}

#define INTERP_BASE 0xbeef0000000

void proc_load_init() {
    process_t* init_proc = proc_new("init", nullptr);
    init_proc->cwd = vfs_get_root();
    // TODO: REF cwd

    vnode_t* tty0_vn = nullptr;
    ASSERT(vfs_lookup(ABS_PATH("/dev/tty0"), &tty0_vn) == 0);

    vnode_t* opened = nullptr;
    ASSERT(vfs_open(tty0_vn, O_RDWR, &opened) == 0);

    init_proc->ctty_vnode = opened;

    file_t* f = file_alloc(opened, O_RDWR);
    ASSERT(f);

    // STDIN
    ASSERT(fd_install(init_proc->fd_table, f, 0) == 0);

    // STDOUT
    file_ref(f);
    ASSERT(fd_install(init_proc->fd_table, f, 0) == 1);

    // STDERR
    file_ref(f);
    ASSERT(fd_install(init_proc->fd_table, f, 0) == 2);


    elf_info_t prog_info;
    elf_info_t interp_info;

    path_t elf_path = ABS_PATH("/usr/bin/bash");
    int err = elf_load(elf_path, init_proc->address_space, &prog_info, 0);
    ASSERT(err == 0);
    logln(LOG_INFO, "INIT", "Loaded %s", elf_path.path);

    [[maybe_unused]] uintptr_t entry = prog_info.entry_point;

    if (prog_info.interpreter_path) {
        err = elf_load(ABS_PATH(prog_info.interpreter_path), init_proc->address_space, &interp_info, INTERP_BASE);
        ASSERT(interp_info.interpreter_path == nullptr);
        ASSERT(err == 0);
        logln(LOG_INFO, "INIT", "Loaded %s", prog_info.interpreter_path);

        entry = interp_info.entry_point;
    }

    char* argv[] = {(char*) elf_path.path, "+m", nullptr};
    char* envp[] = {"PATH=/usr/bin", "TERM=xterm", nullptr}; // "MLIBC_RTLD_DEBUG=1", "MLIBC_RTLD_DEBUG_VERBOSE=1",

    uintptr_t user_sp = elf_prepare_stack(init_proc->address_space, &prog_info, &interp_info, argv, envp);

    thread_t* main_thread = thread_create_user(init_proc, "init", entry, user_sp);

    bool prev = spinlock_lock(&init_proc->lock);
    list_append(&init_proc->threads, &main_thread->sched_list_node);
    spinlock_unlock(&init_proc->lock, prev);

    sched_schedule_thread(main_thread);
}

void proc_init() {
    proc_cache = slab_create_cache("proc", sizeof(process_t), PAGE_SIZE);
}
