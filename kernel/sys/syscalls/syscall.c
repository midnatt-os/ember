#include "sys/syscalls/syscall.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/errno.h"
#include "common/log.h"
#include "cpu/msr.h"
#include "fs/vfs.h"
#include "lib/mem.h"
#include "mem/heap.h"
#include "mem/hhdm.h"
#include "mem/page.h"
#include "mem/ptm.h"
#include "mem/vm.h"
#include "sched/proc.h"
#include "sched/sched.h"
#include "sched/thread.h"

#include <stddef.h>
#include <stdint.h>

#define SYS_ERR(ERR) ((syscall_result_t) { .value = 0, .error = (ERR) })
#define SYS_OK(VAL) ((syscall_result_t) { .value = (VAL), .error = 0 })


static bool is_user_range(uintptr_t ptr, size_t len) {
    uintptr_t end;
    if (__builtin_add_overflow(ptr, len, &end)) {
        return false;
    }

    return (ptr >= USERSPACE_START && end <= USERSPACE_END);
}

int copy_from_user(void* k_dest, uintptr_t u_src, size_t len, vm_address_space_t* as) {
    if (!is_user_range(u_src, len)) {
        return -1;
    }

    char* dst = (char*) k_dest;
    size_t remaining = len;
    uintptr_t curr_u = u_src;

    while (remaining > 0) {
        uintptr_t phys = ptm_virt_to_phys(as, curr_u);
        if (phys == 0) {
            return -1;
        }

        size_t offset = curr_u % PAGE_SIZE;
        size_t chunk_size = PAGE_SIZE - offset;
        if (chunk_size > remaining) {
            chunk_size = remaining;
        }

        void* hhdm_src = (void*) HHDM(phys);
        memcpy(dst, hhdm_src, chunk_size);

        curr_u += chunk_size;
        dst += chunk_size;
        remaining -= chunk_size;
    }

    return 0;
}

int copy_to_user(uintptr_t u_dest, const void* k_src, size_t len, vm_address_space_t* as) {
    if (!is_user_range(u_dest, len)) {
        return -1;
    }

    const char* src = (const char*) k_src;
    size_t remaining = len;
    uintptr_t curr_u = u_dest;

    while (remaining > 0) {
        uintptr_t phys = ptm_virt_to_phys(as, curr_u);
        if (phys == 0) {
            return -1;
        }

        size_t offset = curr_u % PAGE_SIZE;
        size_t chunk_size = PAGE_SIZE - offset;
        if (chunk_size > remaining) {
            chunk_size = remaining;
        }

        void* hhdm_dst = (void*) HHDM(phys);
        memcpy(hhdm_dst, src, chunk_size);

        curr_u += chunk_size;
        src += chunk_size;
        remaining -= chunk_size;
    }

    return 0;
}

ssize_t copy_str_from_user(char* k_dest, uintptr_t u_src, size_t max_len, vm_address_space_t* as) {
    size_t i = 0;

    while (i < max_len) {
        uintptr_t curr_u = u_src + i;

        if (curr_u < USERSPACE_START || curr_u > USERSPACE_END) {
            return -1;
        }

        uintptr_t phys = ptm_virt_to_phys(as, curr_u);
        if (phys == 0) {
            return -1;
        }

        char* hhdm_src = (char*) HHDM(phys);
        k_dest[i] = *hhdm_src;

        if (k_dest[i] == '\0') {
            return (ssize_t) i;
        }

        i++;
    }

    if (max_len > 0)
        k_dest[max_len - 1] = '\0';

    return (ssize_t) max_len;
}

[[noreturn]] syscall_result_t sys_exit(syscall_args_t* args) {
    int code = args->arg0;
    bool panicked = args->arg1;

    thread_t* t = sched_get_current_thread();

    logln(LOG_DEBUG, "SYS_EXIT", "tid: %lu, pid: %lu name: %s, code: %i, panicked: %s", t->tid, t->proc->pid, t->name, code, panicked ? "true" : "false");

    sched_yield(STATUS_DONE);
    ASSERT_UNREACHABLE();
}

syscall_result_t sys_debug(syscall_args_t* args) {
    uintptr_t u_ptr = args->arg0;
    size_t u_len = args->arg1;

    thread_t* t = sched_get_current_thread();
    process_t* proc = t->proc;

    if (u_len > 1024) {
        return SYS_ERR(-EINVAL);
    }

    char k_buf[1025];

    if (copy_from_user(k_buf, u_ptr, u_len, proc->address_space) != 0) {
        return SYS_ERR(-EFAULT);
    }

    k_buf[u_len] = '\0';

    logln(LOG_DEBUG, "SYS_DEBUG", "%s", k_buf);

    return SYS_OK(0);
}

syscall_result_t sys_set_tcb(syscall_args_t* args) {
    void* ptr = (void*) args->arg0;

    logln(LOG_DEBUG, "SYS_SET_TCB", "ptr: %#p", ptr);

    msr_write(MSR_FS_BASE, (uint64_t) ptr);
    thread_t* t = sched_get_current_thread();
    t->state.fs = (uintptr_t) ptr;

    return SYS_OK(0);
}

syscall_result_t sys_openat(syscall_args_t* args) {
    int dirfd = (int) args->arg0;
    uintptr_t path_ptr = args->arg1;
    int flags = (int) args->arg2;

    thread_t* t = sched_get_current_thread();
    process_t* proc = t->proc;

    char* path = nullptr;
    file_t* dir_file = nullptr;
    file_t* file = nullptr;
    vnode_t* vnode = nullptr;
    int fd = -1;
    int err = 0;

    path = heap_alloc(2048);
    if (copy_str_from_user(path, path_ptr, 2048, proc->address_space) < 0) {
        err = -EFAULT;
        goto cleanup;
    }

    logln(LOG_DEBUG, "SYS_OPENAT", "dirfd=%d, path=\"%s\", flags=0x%x", dirfd, path, flags);

    path_t p;
    if (path[0] == '/') {
        p = ABS_PATH(path);
    } else if (dirfd == AT_FDCWD) {
        p = REL_PATH(proc->cwd, path);
    } else {
        dir_file = fd_get(proc->fd_table, dirfd);
        if (!dir_file) {
            err = -EBADF;
            goto cleanup;
        }

        p = REL_PATH(dir_file->vnode, path);
    }


    // 1. Attempt to find the file
    err = vfs_lookup(p, &vnode);

    if (err == 0) {
        // File exists. If O_CREAT and O_EXCL are both set, this is an error.
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            err = -EEXIST;
            goto cleanup;
        }
    } else if (err == -ENOENT && (flags & O_CREAT)) {
        // 2. File doesn't exist, try to create it
        err = vfs_create(p);
        if (err == 0) {
            // Created, now we must lookup again to get the actual vnode
            err = vfs_lookup(p, &vnode);
        }
    }

    // If lookup or creation failed (or O_EXCL triggered)
    if (err != 0)
        goto cleanup;

    file = file_alloc(vnode, flags);
    if (!file) {
        err = -ENOMEM;
        goto cleanup;
    }
    vnode = nullptr;

    fd = fd_install(proc->fd_table, file, 0);
    if (fd < 0) {
        err = fd;
        goto cleanup;
    }

    // Success path
    heap_free(path, 2048); // TODO: wtf?
    if (dir_file)
        file_put(dir_file);

    return SYS_OK(fd);

cleanup:
    if (path)
        heap_free(path, 2048); // TODO: wtf?

    if (dir_file)
        file_put(dir_file);

    if (file)
        file_put(file);

    /* TODO:
     * if (vnode)
        vfs_vnode_put(vnode);*/

    return SYS_ERR(err);
}

syscall_result_t sys_close(syscall_args_t* args) {
    int fd = (int) args->arg0;

    logln(LOG_DEBUG, "SYS_CLOSE", "fd: %d", fd);

    process_t* proc = sched_get_current_thread()->proc;

    int err = fd_close(proc->fd_table, fd);

    if (err != 0)
        return SYS_ERR(err);

    return SYS_OK(0);
}

syscall_result_t sys_read(syscall_args_t* args) {
    int fd = (int) args->arg0;
    uintptr_t u_buf = args->arg1;
    size_t count = args->arg2;

    logln(LOG_DEBUG, "SYS_READ", "fd: %d, buf: %#p, count: %lu", fd, u_buf, count);

    thread_t* t = sched_get_current_thread();
    process_t* proc = t->proc;

    if (!is_user_range(u_buf, count))
        return SYS_ERR(-EFAULT);

    file_t* file = fd_get(proc->fd_table, fd);
    if (!file) {
        return SYS_ERR(-EBADF);
    }

    void* k_buf = vm_map_anon(&global_as, 0, ALIGN_UP(count, PAGE_SIZE), 0, VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_DEFAULT);
    if (!k_buf) {
        file_put(file);
        return SYS_ERR(-ENOMEM);
    }

    ssize_t result = file_read(file, k_buf, count);

    if (result > 0) {
        if (copy_to_user(u_buf, k_buf, result, proc->address_space) != 0) {
            result = -EFAULT;
        }
    }

    vm_unmap(&global_as, k_buf, ALIGN_UP(count, PAGE_SIZE));
    file_put(file);

    if (result >= 0)
        return SYS_OK(result);

    return SYS_ERR(result);
}

syscall_result_t sys_lseek(syscall_args_t* args) {
    int fd = (int) args->arg0;
    off_t offset = (off_t) args->arg1;
    int whence = (int) args->arg2;

    thread_t* t = sched_get_current_thread();
    process_t* proc = t->proc;

    logln(LOG_DEBUG, "SYS_LSEEK", "fd=%d, offset=%ld, whence=%d", fd, offset, whence);

    file_t* file = fd_get(proc->fd_table, fd);
    if (!file)
        return SYS_ERR(-EBADF);

    off_t res = file_seek(file, offset, whence);

    file_put(file);

    if (res < 0)
        return SYS_ERR((int) res);

    return SYS_OK((uintptr_t) res);
}

syscall_result_t sys_mmap(syscall_args_t* args) {
    void* hint = (void*) args->arg0;
    size_t size = args->arg1;
    int prot_flags = args->arg2;
    int flags = args->arg3;
    int fd = args->arg4;
    off_t offset = args->arg5;

    logln(LOG_DEBUG, "SYS_MMAP", "hint: %#p, size: %#lx, prot: %#x, flags: %#x, fd: %d, offset: %lu", hint, size, prot_flags, flags, fd, offset);

    if ((~KNOWN_FLAGS & flags))
        logln(LOG_WARN, "SYS_MMAP", "unknown flags: %x", flags);

    if ((~KNOWN_PROT & prot_flags))
        return SYS_ERR(-EINVAL);

    if ((flags & MAP_PRIVATE) && (flags & MAP_SHARED))
        return SYS_ERR(-EINVAL);

    if (size == 0 || (uintptr_t) hint % PAGE_SIZE)
        return SYS_ERR(-EINVAL);

    vm_prot_t prot = libc_to_vm_prot(prot_flags);

    if ((flags & MAP_ANON) == 0) {
        logln(LOG_WARN, "SYS_MMAP", "We can only handle MAP_ANONYMOUS at the moment", flags);
        return SYS_ERR(-ENOSYS);
    }

    thread_t* t = sched_get_current_thread();
    vm_address_space_t* as = t->proc->address_space;

    uint64_t vm_flags = VM_FLAG_ZERO;
    if (flags & MAP_FIXED)
        vm_flags |= VM_FLAG_FIXED;

    ASSERT(flags & MAP_ANON);
    void* ptr = vm_map_anon(as, hint, size, 0, prot, VM_CACHING_WRITE_BACK, vm_flags);

    if (ptr == nullptr)
        return SYS_ERR(-ENOMEM);

    return SYS_OK((uintptr_t) ptr);
}

syscall_result_t sys_munmap(syscall_args_t* args) {
    void* ptr = (void*) args->arg0;
    size_t size = args->arg1;

    if (ptr == nullptr || ((uintptr_t) ptr) % PAGE_SIZE != 0 || size == 0 || size % PAGE_SIZE != 0) {
        return SYS_ERR(-EINVAL);
    }

    thread_t* t = sched_get_current_thread();
    vm_address_space_t* as = t->proc->address_space;

    vm_unmap(as, ptr, size);

    logln(LOG_DEBUG, "SYS_MUNMAP", "ptr: %#p, size: %#lx", ptr, size);

    return SYS_OK(0);
}

syscall_result_t sys_mprotect(syscall_args_t* args) {
    void* ptr = (void*) args->arg0;
    size_t size = args->arg1;
    int prot_flags = args->arg2;

    thread_t* t = sched_get_current_thread();
    vm_address_space_t* as = t->proc->address_space;

    vm_prot_t prot = libc_to_vm_prot(prot_flags);

    logln(LOG_DEBUG, "SYS_MPROTECT", "ptr: %#p, size: %#lx, prot: %#x", ptr, size, prot);

    vm_protect(as, ptr, size, prot);

    return SYS_OK(0);
}

syscall_handler_t syscall_table[] = { [0] = sys_exit, [1] = sys_debug, [2] = sys_set_tcb, [3] = sys_openat, [4] = sys_close, [5] = sys_read, [6] = sys_lseek, [7] = sys_mmap, [8] = sys_munmap, [9] = sys_mprotect };

uint64_t syscall_table_len = sizeof(syscall_table) / sizeof(syscall_table[0]);
