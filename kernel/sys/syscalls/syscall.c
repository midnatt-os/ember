#include "sys/syscalls/syscall.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/errno.h"
#include "common/log.h"
#include "cpu/msr.h"
#include "fs/fd.h"
#include "fs/vfs.h"
#include "io/poll.h"
#include "lib/mem.h"
#include "lib/string.h"
#include "mem/heap.h"
#include "mem/hhdm.h"
#include "mem/page.h"
#include "mem/ptm.h"
#include "mem/vm.h"
#include "sched/proc.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "sys/time.h"

#include <stddef.h>
#include <stdint.h>

#define SYS_ERR(ERR) ((syscall_result_t) {.value = 0, .error = (ERR)})
#define SYS_OK(VAL) ((syscall_result_t) {.value = (VAL), .error = 0})


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

    logln(LOG_WARN, "SYS_DEBUG", "%s", k_buf);

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

    vnode_t* opened_vn = nullptr;
    err = vfs_open(vnode, flags, &opened_vn);
    if (err)
        goto cleanup;

    file = file_alloc(opened_vn, flags);
    if (!file) {
        err = -ENOMEM;
        goto cleanup;
    }

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

syscall_result_t sys_ioctl(syscall_args_t* args) {
    int fd = (int) args->arg0;
    uint64_t req = (uint64_t) args->arg1;
    uintptr_t u_arg = (uintptr_t) args->arg2;

    logln(LOG_DEBUG, "SYS_IOCTL", "fd=%d req=%#lx arg=%#p", fd, req, (void*) u_arg);

    thread_t* t = sched_get_current_thread();
    process_t* proc = t->proc;

    file_t* file = fd_get(proc->fd_table, fd);
    if (!file)
        return SYS_ERR(-EBADF);

    vnode_t* vn = file->vnode;
    if (!vn) {
        file_put(file);
        return SYS_ERR(-EIO);
    }

    int r = vfs_ioctl(vn, req, u_arg);

    file_put(file);

    if (r < 0)
        return SYS_ERR(r);

    return SYS_OK((uintptr_t) r);
}


syscall_result_t sys_write(syscall_args_t* args) {
    int fd = (int) args->arg0;
    uintptr_t u_buf = (uintptr_t) args->arg1;
    size_t count = (size_t) args->arg2;

    logln(LOG_DEBUG, "SYS_WRITE", "fd: %d, buf: %#p, count: %lu", fd, (void*) u_buf, count);

    thread_t* t = sched_get_current_thread();
    process_t* proc = t->proc;

    if (count == 0)
        return SYS_OK(0);

    if (!is_user_range(u_buf, count))
        return SYS_ERR(-EFAULT);

    file_t* file = fd_get(proc->fd_table, fd);
    if (!file)
        return SYS_ERR(-EBADF);

    void* k_buf = vm_map_anon(&global_as, 0, ALIGN_UP(count, PAGE_SIZE), 0, VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_DEFAULT);
    if (!k_buf) {
        file_put(file);
        return SYS_ERR(-ENOMEM);
    }

    if (copy_from_user(k_buf, u_buf, count, proc->address_space) != 0) {
        vm_unmap(&global_as, k_buf, ALIGN_UP(count, PAGE_SIZE));
        file_put(file);
        return SYS_ERR(-EFAULT);
    }

    ssize_t result = file_write(file, k_buf, count);

    vm_unmap(&global_as, k_buf, ALIGN_UP(count, PAGE_SIZE));
    file_put(file);

    if (result >= 0)
        return SYS_OK((uintptr_t) result);

    return SYS_ERR((int) result);
}


syscall_result_t sys_getpid(syscall_args_t* _) {
    uint64_t pid = sched_get_current_thread()->proc->pid;
    return SYS_OK(pid);
}

typedef uint32_t mode_t;
typedef int64_t off_t;

#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFLNK 0120000

#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001


typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    mode_t st_mode;
    uint64_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    off_t st_size;

    int64_t st_blksize;
    int64_t st_blocks;

    int64_t st_atime;
    int64_t st_mtime;
    int64_t st_ctime;
} ustat_t;


static mode_t vnode_mode(vnode_t* vn) {
    mode_t type = 0;
    switch (vn->type) {
        case V_REG: type = S_IFREG; break;
        case V_DIR: type = S_IFDIR; break;
        case V_CHR: type = S_IFCHR; break;
        case V_BLK: type = S_IFBLK; break;
        case V_LNK: type = S_IFLNK; break;
        default:    type = S_IFREG; break;
    }

    // permissive defaults for now
    mode_t perm = 0;
    if (vn->type == V_DIR)
        perm = 0777;
    else
        perm = 0666;

    return type | perm;
}

static int fill_user_stat(vnode_t* vn, ustat_t* out) {
    memset(out, 0, sizeof(*out));

    stat_t st;
    int err = vfs_getattr(vn, &st);
    if (err)
        return err;

    out->st_mode = vnode_mode(vn);
    out->st_size = (off_t) st.size;

    // everything else 0 for now (dev, ino, times, etc.)
    return 0;
}

syscall_result_t sys_fstat(syscall_args_t* args) {
    int fd = (int) args->arg0;
    uintptr_t u_statbuf = (uintptr_t) args->arg1;

    logln(LOG_DEBUG, "SYS_FSTAT", "fd=%d statbuf=%#p", fd, (void*) u_statbuf);

    thread_t* t = sched_get_current_thread();
    process_t* proc = t->proc;

    if (!is_user_range(u_statbuf, sizeof(ustat_t)))
        return SYS_ERR(-EFAULT);

    file_t* file = fd_get(proc->fd_table, fd);
    if (!file)
        return SYS_ERR(-EBADF);

    vnode_t* vn = file->vnode;
    if (!vn) {
        file_put(file);
        return SYS_ERR(-EIO);
    }

    ustat_t kst;
    int err = fill_user_stat(vn, &kst);
    if (!err) {
        if (copy_to_user(u_statbuf, &kst, sizeof(kst), proc->address_space) != 0)
            err = -EFAULT;
    }

    file_put(file);

    if (err)
        return SYS_ERR(err);
    return SYS_OK(0);
}

#define AT_SYMLINK_NOFOLLOW 0x100

syscall_result_t sys_fstatat(syscall_args_t* args) {
    int dirfd = (int) args->arg0;
    uintptr_t path_ptr = (uintptr_t) args->arg1;
    uintptr_t u_statbuf = (uintptr_t) args->arg2;
    int flags = (int) args->arg3;

    thread_t* t = sched_get_current_thread();
    process_t* proc = t->proc;

    if (!is_user_range(u_statbuf, sizeof(ustat_t)))
        return SYS_ERR(-EFAULT);

    char* path = (char*) heap_alloc(2048);
    if (!path)
        return SYS_ERR(-ENOMEM);

    if (copy_str_from_user(path, path_ptr, 2048, proc->address_space) < 0) {
        heap_free(path, 2048);
        return SYS_ERR(-EFAULT);
    }

    logln(LOG_DEBUG, "SYS_FSTATAT", "dirfd=%d path=%s statbuf=%#p flags=%#x", dirfd, (void*) path, (void*) u_statbuf, flags);

    path_t p;
    file_t* dir_file = nullptr;

    if (path[0] == '/') {
        p = ABS_PATH(path);
    } else if (dirfd == AT_FDCWD) {
        p = REL_PATH(proc->cwd, path);
    } else {
        dir_file = fd_get(proc->fd_table, dirfd);
        if (!dir_file) {
            heap_free(path, 2048);
            return SYS_ERR(-EBADF);
        }
        p = REL_PATH(dir_file->vnode, path);
    }

    uint32_t lflags = (flags & AT_SYMLINK_NOFOLLOW) ? 0 : VFS_LOOKUP_FOLLOW_LAST;

    vnode_t* vn = nullptr;
    int err = vfs_lookup_ext(p, lflags, &vn);

    ustat_t kst;
    if (!err)
        err = fill_user_stat(vn, &kst);

    if (!err) {
        if (copy_to_user(u_statbuf, &kst, sizeof(kst), proc->address_space) != 0)
            err = -EFAULT;
    }

    if (dir_file)
        file_put(dir_file);

    heap_free(path, 2048);

    if (err)
        return SYS_ERR(err);
    return SYS_OK(0);
}

syscall_result_t sys_getppid(syscall_args_t* _) {
    process_t* proc = sched_get_current_thread()->proc;

    logln(LOG_DEBUG, "SYS_GETPPID", "(current pid: %lu)", proc->pid);

    if (proc->parent == nullptr)
        return SYS_OK(0);

    return SYS_OK(proc->parent->pid);
}

syscall_result_t sys_getpgid(syscall_args_t* args) {
    int pid = (int) args->arg0;
    logln(LOG_DEBUG, "SYS_GETPGID", "pid=%lu", pid);
    process_t* self = sched_get_current_thread()->proc;

    if (pid != 0 && pid != (int) self->pid)
        return SYS_ERR(-ESRCH);

    return SYS_OK(self->pid);
}

syscall_result_t sys_dup(syscall_args_t* args) {
    int fd = args->arg0;
    int flags = args->arg1;
    logln(LOG_DEBUG, "SYS_DUP", "fd: %d, flags: %d", fd, flags);

    process_t* proc = sched_get_current_thread()->proc;

    int newfd = fd_dup(proc->fd_table, fd);

    if (newfd < 0)
        return SYS_ERR(newfd);

    return SYS_OK(newfd);
}

syscall_result_t sys_gethostname(syscall_args_t* args) {
    uintptr_t ubuf = args->arg0;
    size_t ubuf_size = args->arg1;

    process_t* proc = sched_get_current_thread()->proc;
    logln(LOG_DEBUG, "SYS_GETHOSTNAME", "pid: %lu", proc->pid);

    if (!is_user_range(ubuf, ubuf_size))
        return SYS_ERR(-EFAULT);

    // TODO: don't hard code this
    const char* hostname = "midnatt";
    ASSERT(ubuf_size >= strlen(hostname));
    strcpy((char*) ubuf, hostname);

    return SYS_OK(0);
}

syscall_result_t sys_setpgid(syscall_args_t* args) {
    uint64_t pid = args->arg0;
    uint64_t pgid = args->arg1;

    logln(LOG_DEBUG, "SYS_SETPGID", "pid: %lu, pgid: %lu", pid, pgid);

    return SYS_OK(0);
}


syscall_result_t sys_ppoll(syscall_args_t* args) {
    pollfd_t* ufds = (pollfd_t*) args->arg0;
    size_t nfds = (size_t) args->arg1;
    timespec_t* utimeout = (timespec_t*) args->arg2;
    // const sigset_t* sigmask = (const sigset_t*)args->arg3; // TODO

    process_t* proc = sched_get_current_thread()->proc;

    if (nfds > POLL_MAX_FDS)
        return SYS_ERR(-EINVAL);

    if (nfds > 0 && ufds == nullptr)
        return SYS_ERR(-EFAULT);

    timespec_t timeout = {0};
    timespec_t* ktimeout_ptr = nullptr;

    if (utimeout != nullptr) {
        if (copy_from_user(&timeout, (uintptr_t) utimeout, sizeof(timeout), proc->address_space) != 0)
            return SYS_ERR(-EFAULT);

        if (!timespec_is_valid(&timeout))
            return SYS_ERR(-EINVAL);

        ktimeout_ptr = &timeout;
    }

    logln(LOG_DEBUG, "SYS_PPOLL", "ufds:%#p nfds:%lu utimeout:%#p (sigmask ignored)", ufds, nfds, utimeout);

    // Special case: nfds == 0 => just sleep for timeout, then return 0
    // If timeout is 'nullptr' => sleep forever (until signals are implemented)
    if (nfds == 0) {
        if (ktimeout_ptr && timespec_is_zero(ktimeout_ptr))
            return SYS_OK(0);

        if (ktimeout_ptr) {
            sched_sleep(timespec_to_ns(ktimeout_ptr));
            return SYS_OK(0);
        }

        // TODO: until signals
        while (true)
            sched_yield(STATUS_BLOCKED);
    }

    size_t bytes = nfds * sizeof(pollfd_t);
    pollfd_t* kfds = (pollfd_t*) heap_alloc(bytes);
    if (!kfds)
        return SYS_ERR(-ENOMEM);

    if (copy_from_user(kfds, (uintptr_t) ufds, bytes, proc->address_space) != 0) {
        heap_free(kfds, bytes);
        return SYS_ERR(-EFAULT);
    }

    for (size_t i = 0; i < nfds; i++) {
        kfds[i].revents = 0;
    }

    int rc = io_ppoll(kfds, nfds, ktimeout_ptr);

    if (rc >= 0) {
        if (copy_to_user((uintptr_t) ufds, kfds, bytes, proc->address_space) != 0) {
            heap_free(kfds, bytes);
            return SYS_ERR(-EFAULT);
        }

        heap_free(kfds, bytes);
        return SYS_OK((uint64_t) rc);
    }

    heap_free(kfds, bytes);
    return SYS_ERR(rc);
}

syscall_handler_t syscall_table[] = {
    [0] = sys_exit,   [1] = sys_debug,   [2] = sys_set_tcb, [3] = sys_openat,   [4] = sys_close,    [5] = sys_read,     [6] = sys_lseek, [7] = sys_mmap,         [8] = sys_munmap,   [9] = sys_mprotect, [10] = sys_ioctl,
    [11] = sys_write, [12] = sys_getpid, [13] = sys_fstat,  [14] = sys_fstatat, [15] = sys_getppid, [16] = sys_getpgid, [17] = sys_dup,  [18] = sys_gethostname, [19] = sys_setpgid, [20] = sys_ppoll
};

uint64_t syscall_table_len = sizeof(syscall_table) / sizeof(syscall_table[0]);
