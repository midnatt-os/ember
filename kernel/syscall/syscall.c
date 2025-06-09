#include "syscall/syscall.h"

#include <stddef.h>
#include <stdint.h>

#include "abi/fcntl.h"
#include "abi/seek_whence.h"
#include "abi/syscall/syscall.h"
#include "common/assert.h"
#include "common/lock/mutex.h"
#include "common/log.h"
#include "common/types.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/registers.h"
#include "fs/fd.h"
#include "fs/file.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "memory/heap.h"
#include "memory/hhdm.h"
#include "memory/vm.h"
#include "sched/process.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "abi/errno.h"
#include "sys/framebuffer.h"

#define MSR_EFER_SCE (1 << 0)

#define MAX_DEBUG_STR_LEN 512

#define SYS_RES(VAL, ERR) ((SyscallResult) { .value = VAL, .error = ERR })



extern void syscall_entry();

size_t copy_buffer_to_user(void* dest, void* src, size_t len) {
    Process* proc = sched_get_current_process();
    return vm_copy_to(proc->as, (uintptr_t) dest, src, len);
}

void* copy_buffer_from_user(void* src, size_t len) {
    void* buffer = kmalloc(len);

    VmAddressSpace* as = sched_get_current_process()->as;
    size_t bytes_read = vm_copy_from(buffer, as, (uintptr_t) src, len);

    if (bytes_read != len) {
        kfree(buffer);
        return nullptr;
    }

    return buffer;
}

void* copy_string_from_user(char* src, size_t length) {
    char* str = copy_buffer_from_user(src, length + 1);

    if (str == nullptr)
        return nullptr;

    str[length] = '\0';
    return str;
}

void syscall_init() {
    write_msr(MSR_EFER, read_msr(MSR_EFER) | MSR_EFER_SCE);
    write_msr(MSR_STAR, ((uint64_t) GDT_SELECTOR_CODE64_RING0 << 32) | ((uint64_t) (GDT_SELECTOR_DATA64_RING3 - 8) << 48));
    write_msr(MSR_LSTAR, (uint64_t) syscall_entry);
    write_msr(MSR_SFMASK, read_msr(MSR_SFMASK) | (1 << 9));
}

[[noreturn]] void syscall_exit(int code, bool panicked) {
    Thread* t = sched_get_current_thread();

    logln(LOG_DEBUG, "SYSCALL", "exit(tid: %li, name: %s, code: %i, panicked: %s)", t->tid, t->name, code, panicked ? "true" : "false");

    sched_yield(STATUS_DONE);
    ASSERT_UNREACHABLE();
}

SyscallResult syscall_debug(char* str, size_t length) {
    SyscallResult res = {};

    if (length > MAX_DEBUG_STR_LEN)
        logln(LOG_WARN, "SYSCALL_DEBUG", "Debug str limit reached (got: %lu chars, max: %lu)", length, MAX_DEBUG_STR_LEN);

    str = copy_string_from_user(str, length);
    if (str == nullptr) {
        res.error = -EINVAL;
        return res;
    }

    logln(LOG_WARN, "SYSCALL_DEBUG", "%s", str);

    return res;
}

SyscallResult syscall_set_tcb(void* ptr) {
    SyscallResult res = {};

    logln(LOG_DEBUG, "SYSCALL", "set_tcb(ptr: %#p)", ptr);
    write_msr(MSR_FS_BASE, (uint64_t) ptr);

    return res;
}

SyscallResult syscall_anon_alloc(size_t size) {
    SyscallResult res = {};

    if (size == 0 || size % PAGE_SIZE != 0) {
        res.error = -EINVAL;
        return res;
    }

    VmAddressSpace* as = sched_get_current_process()->as;

    void* ptr = vm_map_anon(as, nullptr, size, VM_PROT_RW, VM_CACHING_DEFAULT, VM_FLAG_ZERO);
    res.value = (uintptr_t) ptr;

    logln(LOG_DEBUG, "SYSCALL", "anon_alloc(size: %#lx) = %#p", size, ptr);

    return res;
}

SyscallResult syscall_anon_free(void* ptr, size_t size) {
    SyscallResult res = {};

    if (ptr == nullptr || ((uintptr_t) ptr) % PAGE_SIZE != 0 || size == 0 || size % PAGE_SIZE != 0) {
        res.error = -EINVAL;
        return res;
    }

    VmAddressSpace* as = sched_get_current_process()->as;

    vm_unmap(as, ptr, size);

    logln(LOG_DEBUG, "SYSCALL", "anon_free(ptr: %#p, size: %#lx)", ptr, size);

    return res;
}


SyscallResult syscall_open(char* path, [[maybe_unused]]size_t path_length, int flags, [[maybe_unused]] unsigned int mode) {
    logln(LOG_DEBUG, "SYSCALL", "open(path: %s, flags: %d, mode: %o)", path, flags, mode);

    const char* file_path = copy_string_from_user(path, path_length);

    VNode* node;
    int err;

    Process* proc = sched_get_current_process();
    mutex_acquire(&proc->lock);
    VNode* cwd = proc->cwd;
    mutex_release(&proc->lock);


    if ((err = vfs_lookup(cwd, file_path, &node)) < 0) {
        if ((flags & O_CREAT) == 0)
            return SYS_RES(0, err);

        char parent[MAX_PATH_LEN], name[MAX_NAME_LEN+1];
        if ((err = vfs_split_path(file_path, parent, MAX_PATH_LEN, name, MAX_NAME_LEN + 1)) < 0)
            return SYS_RES(0, err);

        if ((err = vfs_create_file(cwd, parent, name, &node)))
            return SYS_RES(0, err);
    } else {
        if ((flags & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL)) {
            vnode_unref(node);
            return (SyscallResult) { .error = -EEXIST };
        }

        /*

                If O_TRUNC and opened for write → truncate to zero
                if ((flags & O_TRUNC) && (flags & (O_WRONLY|O_RDWR))) {
                    Assume a setattr or truncate helper exists:
                    err = vfs_truncate(proc->cwd, path, 0);
                    if (err < 0) {
                        vnode_unref(vp);
                        return syscall_result_err(err);
                    }
                }

         */
    }

    if ((flags & O_WRONLY) && node->type != VNODE_TYPE_FILE) {
        vnode_unref(node);
        return SYS_RES(0, -EISDIR);
    }

    File *f = file_create(node, flags);
    vnode_unref(node);

    int res = fd_alloc(&proc->fds, f);

    if (res < 0) {
        file_close(f);
        return SYS_RES(0, res);  /* -EMFILE */
    }

    return (SyscallResult) SYS_RES(res, 0);
}

SyscallResult syscall_close(int fd) {
    logln(LOG_DEBUG, "SYSCALL", "close(fd: %d)", fd);

    Process* proc = sched_get_current_process();
    int err  = fd_close(&proc->fds, fd);

    if (err < 0)
        return SYS_RES(0, err);

    return SYS_RES(0, 0);
}

SyscallResult syscall_read(int fd, void* buf, size_t len) {
    logln(LOG_DEBUG, "SYSCALL", "read(fd: %d, buf: %#p, len: %lu)", fd, buf, len);

    Process* proc = sched_get_current_process();
    File* f;

    int err = fd_get(&proc->fds, fd, &f);
    if (err < 0)
        return SYS_RES(0, err);

    void* kbuf = kmalloc(len);

    ssize_t res = file_read(f, kbuf, len);
    if (res < 0) {
        kfree(kbuf);
        return SYS_RES(0, res);
    }

    size_t bytes_copied = copy_buffer_to_user(buf, kbuf, res);
    kfree(kbuf);

    if (bytes_copied != (size_t) res)
        return SYS_RES(0, -EFAULT);

    return SYS_RES(bytes_copied, 0);
}

SyscallResult syscall_write(int fd, void* buf, size_t len) {
    logln(LOG_DEBUG, "SYSCALL", "write(fd: %d, buf: %#p, len: %lu)", fd, buf, len);

    Process* proc = sched_get_current_process();
    File* f;

    int err = fd_get(&proc->fds, fd, &f);
    if (err < 0)
        return SYS_RES(0, err);


    void* kbuf = copy_buffer_from_user(buf, len);
    if (kbuf == nullptr)
        return SYS_RES(0, -EFAULT);

    ssize_t res = file_write(f, kbuf, len);
    kfree(kbuf);

    if (res < 0)
        return SYS_RES(0, res);

    return SYS_RES(res, 0);
}

SyscallResult syscall_seek(int fd, off_t offset, int whence) {
    char* whence_str;
    switch (whence) {
        case SEEK_SET: whence_str = "SEEK_SET"; break;
        case SEEK_CUR: whence_str = "SEEK_CUR"; break;
        case SEEK_END: whence_str = "SEEK_END"; break;
        default:       whence_str = "UNKNOWN"; break;
    }

    logln(LOG_DEBUG, "SYSCALL", "lseek(fd: %d, off: %ld, whence: %s)", fd, offset, whence_str);

    Process* proc = sched_get_current_process();
    File* f;
    int err;

    if ((err = fd_get(&proc->fds, fd, &f)) < 0)
        return SYS_RES(0, err);

    off_t new_off = file_seek(f, offset, whence);
    if (new_off < 0)
        return SYS_RES(0, (int) new_off);

    return SYS_RES((uint64_t) new_off, 0);
}

SyscallResult syscall_fetch_framebuffer(SysFramebuffer* user_fb) {
    Process* proc = sched_get_current_process();
    SysFramebuffer new_fb;

    mutex_acquire(&proc->lock);
    framebuffer_map(proc->as, &new_fb);
    mutex_release(&proc->lock);

    if (copy_buffer_to_user(user_fb, &new_fb, sizeof(SysFramebuffer)) < 0)
        return SYS_RES(0, -EFAULT);

    return SYS_RES(0, 0);
}
