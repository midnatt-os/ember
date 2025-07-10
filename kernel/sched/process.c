#include "sched/process.h"
#include "common/assert.h"
#include "common/lock/mutex.h"
#include "fs/fd.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "lib/list.h"
#include "memory/vm.h"
#include "memory/heap.h"
#include "lib/string.h"
#include "sched/sched.h"
#include "sched/thread.h"



_Atomic uint64_t next_pid = 0;

Process* process_create(const char* name, VmAddressSpace* as) {
    Process* proc = kmalloc(sizeof(Process));

    *proc = (Process) {
        .pid = next_pid++,
        .as = as,
        .children = LIST_NEW,
        .threads = LIST_NEW,
        .lock = MUTEX_NEW
    };

    vfs_root(&proc->cwd);

    ASSERT(vfs_root(&proc->cwd) >= 0);

    fd_init(&proc->fds);

    strcpy(proc->name, name);

    return proc;
}

Process* process_fork(Process* proc_to_fork, SyscallSavedRegs* regs) {
    Process* new = kmalloc(sizeof(Process));

    *new = (Process) {
        .pid = next_pid++,
        .children = LIST_NEW,
        .threads = LIST_NEW,
        .lock = MUTEX_NEW
    };

    mutex_acquire(&proc_to_fork->lock);

    strncpy(new->name, proc_to_fork->name, strlen(proc_to_fork->name));

    VmAddressSpace* new_as = vm_create_address_space();
    vm_clone_address_space(new_as);

    new->as = new_as;

    fd_clone_table(&proc_to_fork->fds, &new->fds);

    new->cwd = proc_to_fork->cwd;
    vnode_ref(new->cwd);

    Thread* t = thread_clone(new, sched_get_current_thread(), regs);

    list_append(&proc_to_fork->children, &new->child_node);
    new->parent = proc_to_fork;

    sched_schedule_thread(t);

    mutex_release(&proc_to_fork->lock);

    return new;
}
