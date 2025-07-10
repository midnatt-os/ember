#include "syscall/syscall.h"

#include <stddef.h>
#include <stdint.h>

#include "abi/fcntl.h"
#include "abi/seek_whence.h"
#include "abi/syscall/syscall.h"
#include "abi/sysv/elf.h"
#include "abi/sysv/sysv.h"
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
#include "lib/string.h"
#include "memory/heap.h"
#include "memory/hhdm.h"
#include "memory/vm.h"
#include "sched/process.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "abi/errno.h"
#include "sys/framebuffer.h"
#include "sys/time.h"

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

#define MAX_ARG_STRLEN 256

char** copy_string_array_from_user(char* const user_array[], uint64_t count) {
    if (count == 0 || count > 128)
        return nullptr;

    char** k_array = kmalloc((count + 1) * sizeof(char*));
    if (!k_array)
        return nullptr;

    VmAddressSpace* as = sched_get_current_process()->as;

    for (uint64_t i = 0; i < count; ++i) {
        uintptr_t user_str_ptr;

        // Copy one pointer (from user argv[i])
        if (vm_copy_from(&user_str_ptr, as, (uintptr_t) &user_array[i], sizeof(user_str_ptr)) != sizeof(user_str_ptr)) {
            goto fail;
        }

        if (user_str_ptr == 0) {
            k_array[i] = NULL;
            continue;
        }

        // Now copy the string pointed to by user_str_ptr
        char* str = copy_string_from_user((char*) user_str_ptr, MAX_ARG_STRLEN);
        if (!str) {
            goto fail;
        }

        k_array[i] = str;
    }

    k_array[count] = NULL;
    return k_array;

fail:
    for (uint64_t j = 0; j < count; ++j) {
        if (k_array[j]) kfree(k_array[j]);
    }
    kfree(k_array);
    return nullptr;
}

void free_string_array(char** array) {
    if (!array) return;
    for (size_t i = 0; array[i]; ++i)
        kfree(array[i]);
    kfree(array);
}

void syscall_init() {
    write_msr(MSR_EFER, read_msr(MSR_EFER) | MSR_EFER_SCE);
    write_msr(MSR_STAR, ((uint64_t) GDT_SELECTOR_CODE64_RING0 << 32) | ((uint64_t) (GDT_SELECTOR_DATA64_RING3 - 8) << 48));
    write_msr(MSR_LSTAR, (uint64_t) syscall_entry);
    write_msr(MSR_SFMASK, read_msr(MSR_SFMASK) | (1 << 9));
}

[[noreturn]] void syscall_exit(int code, bool panicked) {
    Thread* t = sched_get_current_thread();

    logln(LOG_DEBUG, "SYSCALL", "exit(tid: %lu, pid: %lu name: %s, code: %i, panicked: %s)", t->tid, t->proc->pid, t->name, code, panicked ? "true" : "false");

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
    Thread* t = sched_get_current_thread();
    t->state.fs = (uintptr_t) ptr;

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
            return SYS_RES(0, -EEXIST);
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

SyscallResult syscall_mkdir(const char* path, size_t path_len, [[maybe_unused]] uint64_t mode) {
    ASSERT((path = copy_string_from_user((char*) path, path_len)) != nullptr);

    logln(LOG_DEBUG, "SYSCALL", "mkdir(path: %s, mode: %o)", path, mode);

    char parent[128];
    char name[128];
    ASSERT(vfs_split_path(path, parent, sizeof(parent), name, sizeof(name)) == 0);

    VNode* n;
    int res = vfs_create_dir(nullptr, parent, name, &n);
    if (res < 0)
        return SYS_RES(0, res);

    return SYS_RES(res, 0);
}

SyscallResult syscall_dup(int fd) {
    logln(LOG_DEBUG, "SYSCALL", "dup(fd: %d)", fd);

    Process* proc = sched_get_current_process();

    int res = fd_dup(&proc->fds, fd);

    if (res < 0)
        return SYS_RES(0, res);

    return SYS_RES(res, 0);
}

SyscallResult syscall_dup2(int fd, int newfd) {
    logln(LOG_DEBUG, "SYSCALL", "dup2(fd: %d, newfd: %d)", fd, newfd);

    if (fd == newfd)
        return SYS_RES(fd, 0);

    Process* proc = sched_get_current_process();

    int res = fd_dup2(&proc->fds, fd, newfd);

    if (res < 0)
        return SYS_RES(0, res);

    return SYS_RES(res, 0);
}

SyscallResult syscall_fetch_framebuffer(SysFramebuffer* user_fb) {
    logln(LOG_DEBUG, "SYSCALL", "fetch_framebuffer()");

    Process* proc = sched_get_current_process();
    SysFramebuffer new_fb;

    mutex_acquire(&proc->lock);
    framebuffer_map(proc->as, &new_fb);
    mutex_release(&proc->lock);

    if (copy_buffer_to_user(user_fb, &new_fb, sizeof(SysFramebuffer)) < 0)
        return SYS_RES(0, -EFAULT);

    return SYS_RES(0, 0);
}

SyscallResult syscall_fork() {
    Thread* current_thread = sched_get_current_thread();

    logln(LOG_DEBUG, "SYSCALL", "syscall_fork() (pid: %lu, tid: %lu)", current_thread->proc->pid, current_thread->tid);

    [[maybe_unused]] SyscallSavedRegs* regs = (SyscallSavedRegs*) (current_thread->kernel_stack.base - sizeof(SyscallSavedRegs));
    return SYS_RES(0, 0);
    Process* child_proc = process_fork(sched_get_current_process(), regs);

    return SYS_RES(child_proc->pid, 0);
}

SyscallResult syscall_execve(const char *path, size_t path_len, char *const argv[], uint64_t argv_len, char *const envp[], uint64_t envp_len) {
    Thread* t = sched_get_current_thread();

    path = copy_string_from_user((char*) path, path_len);

    logln(LOG_DEBUG, "SYSCALL", "syscall_execve(path: %s) (pid: %lu, tid: %lu)", path, t->proc->pid, t->tid);

    char** k_argv = copy_string_array_from_user(argv, argv_len);
    if (!k_argv) {
        free_string_array(k_argv);
        return SYS_RES(0, -EFAULT);
        }

    char** k_envp = copy_string_array_from_user(envp, envp_len);
    if (!k_envp) {
        free_string_array(k_argv);
        return SYS_RES(0, -EFAULT);
        }

    t->proc->as = vm_create_address_space();


    ElfFile* elf;
    ASSERT(elf_read(path, &elf) == ELF_RESULT_OK);
    ASSERT(elf_load(elf, t->proc->as) == ELF_RESULT_OK);

    uintptr_t entry;
    char* interp_path;
    switch (elf_lookup_interpreter(elf, &interp_path)) {
        case ELF_RESULT_OK:
            ElfFile* interp_elf;
            ASSERT(elf_read(interp_path, &interp_elf) == ELF_RESULT_OK);
            ASSERT(elf_load(interp_elf, t->proc->as) == ELF_RESULT_OK);
            entry = interp_elf->entry_point;
            break;

        case ELF_RESULT_ERR_NOT_FOUND: entry = elf->entry_point; break;

        default: panic("Elf interpreter lookup on execve failed.");
    }

    Auxv auxv = { .entry = elf->entry_point };

    uintptr_t phdr_table;
    if (elf_lookup_phdr_table(elf, &phdr_table) == ELF_RESULT_OK) {
        auxv.phdr = phdr_table;
        auxv.phnum = elf->phdrs.count;
        auxv.phent = elf->phdrs.size;
    }

    kfree(elf);

    #define U_STACK_SIZE 4096 * 8
    uintptr_t user_stack = sysv_setup_stack(t->proc->as, U_STACK_SIZE, k_argv, k_envp, &auxv);

    Thread* new_t = thread_create_user(t->proc, path, entry, user_stack);
    sched_schedule_thread(new_t);

    sched_yield(STATUS_DONE);
    ASSERT_UNREACHABLE();

    return SYS_RES(0, 0);
}

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

static inline VmProtection prot_from_posix(int prot) {
    return (VmProtection) {
        .read  = (prot & PROT_READ)  != 0,
        .write = (prot & PROT_WRITE) != 0,
        .exec  = (prot & PROT_EXEC)  != 0
    };
}

#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

static inline int vm_flags_from_posix(int flags) {
    int vm_flags = VM_FLAG_NONE;

    if (flags & MAP_FIXED)
        vm_flags |= VM_FLAG_FIXED;

    if (flags & MAP_ANONYMOUS)
        vm_flags |= VM_FLAG_ZERO;

    return vm_flags;
}

SyscallResult syscall_mmap(void* hint, size_t length, int prot, int flags, int fd, off_t offset) {
    Process* proc = sched_get_current_process();

    logln(LOG_DEBUG, "SYSCALL", "syscall_mmap(hint: %#p, length: %#lx, prot: %d, flags: %d, fd: %d, offset: %lu)",
        hint,
        length,
        prot,
        flags,
        fd,
        offset
    );

    ASSERT(fd == -1);
    ASSERT(offset == 0);

    #define MAP_ANON 0x20
    if (flags & MAP_ANON) {
        void* map_addr = vm_map_anon(proc->as, hint, length, prot_from_posix(prot), VM_CACHING_DEFAULT, vm_flags_from_posix(flags));
        if (map_addr == nullptr)
            return SYS_RES(0, -ENOMEM);

        return SYS_RES((uint64_t) map_addr, 0);
    } else {
        ASSERT_UNREACHABLE();
    }


    return SYS_RES(0, 0);
}

SyscallResult syscall_mprotect(void* pointer, size_t length, int prot_flags) {
    if (((uintptr_t) pointer % PAGE_SIZE) || (length % PAGE_SIZE))
        return SYS_RES(0, -EINVAL);

    VmProtection prot = {
        .read  = !!(prot_flags & PROT_READ),
        .write = !!(prot_flags & PROT_WRITE),
        .exec  = !!(prot_flags & PROT_EXEC),
    };

    logln(LOG_DEBUG, "SYSCALL", "syscall_mprotect(pointer: %#p, length: %#lx, prot: %c%c%c)", pointer, length, prot.read ? 'R' : '-', prot.write ? 'W' : '-', prot.exec ? 'X' : '-');

    int ret = vm_mprotect(sched_get_current_process()->as, pointer, length, prot);

    if (ret < 0)
        return SYS_RES(0, ret);

    return SYS_RES(ret, 0);
}

SyscallResult syscall_gettime(int clock, TimeSpec* ts) {
    TimeSpec k_ts = { .tv_sec = 0, .tv_nsec = 0 };

    switch (clock) {
        case CLOCK_REALTIME:
            copy_buffer_to_user(ts, &k_ts, sizeof(TimeSpec));
            return SYS_RES(0, 0);

        case CLOCK_MONOTONIC:
            ns_to_timespec(time_current(), &k_ts);
            copy_buffer_to_user(ts, &k_ts, sizeof(TimeSpec));

            return SYS_RES(0, 0);

        default:
            logln(LOG_WARN, "SYSCALL", "gettime: unhandled clock type: %d", clock);
            ASSERT_UNREACHABLE();
    }
}

SyscallResult syscall_nsleep(uint64_t ns) {
    logln(LOG_DEBUG, "SYSCALL", "syscall_nsleep(ns: %lu)", ns);
    sched_sleep(ns);
    return SYS_RES(0, 0);
}

SyscallResult syscall_getpid() {
    logln(LOG_DEBUG, "SYSCALL", "syscall_getpid()");
    Process* proc = sched_get_current_process();
    return SYS_RES(proc->pid, 0);
}

SyscallResult syscall_getppid() {
    logln(LOG_DEBUG, "SYSCALL", "syscall_getppid()");
    Process* proc = sched_get_current_process();
    return SYS_RES(proc->parent == nullptr ? 0 : proc->parent->pid, 0);
}

char* root = "/";

SyscallResult syscall_getcwd(char* buffer, [[maybe_unused]] size_t size) {
    logln(LOG_DEBUG, "SYSCALL", "syscall_getcwd()");
    copy_buffer_to_user(buffer, root, 2);
    return SYS_RES(0, 0);
}

SyscallResult syscall_isatty(int fd) {
    logln(LOG_DEBUG, "SYSCALL", "syscall_isatty(fd: %d)", fd);
    Process* proc = sched_get_current_process();

    File* f;
    if (fd_get(&proc->fds, fd, &f) != 0)
        return SYS_RES(0, -EBADF);

    if (f->is_tty)
        return SYS_RES(0, 0);


    return SYS_RES(0, -ENOTTY);
}

SyscallResult syscall_ioctl(int fd, uint64_t request, [[maybe_unused]] void* argp) {
    logln(LOG_DEBUG, "SYSCALL", "ioctl(fd: %d, request: %#lx)", fd, request);
    Process* proc = sched_get_current_process();

    File* f;
    if (fd_get(&proc->fds, fd, &f) != 0)
        return SYS_RES(0, -EBADF);

    int res = file_ioctl(f, request, argp);
    if (res < 0)
        return SYS_RES(0, res);

    return SYS_RES(res, 0);
}

SyscallResult syscall_fcntl(int fd, int request, uintptr_t arg) {
    logln(LOG_DEBUG, "SYSCALL", "fcntl(fd: %d, request: %lu, arg: %#p)", fd, request, arg);

    return SYS_RES(0, 0);
}

struct stat {
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_nlink;
	uint32_t st_mode;
	uint64_t st_uid;
	uint64_t st_gid;
	unsigned int __pad0;
	uint64_t st_rdev;
	off_t st_size;
	uint64_t st_blksize;
	uint64_t st_blocks;
	TimeSpec st_atim;
	TimeSpec st_mtim;
	TimeSpec st_ctim;
	long __unused[3];
};

SyscallResult syscall_stat(int fd, struct stat* statbuf) {
    logln(LOG_DEBUG, "SYSCALL", "stat(fd: %d)", fd);
    Process* proc = sched_get_current_process();

    File* f;
    if (fd_get(&proc->fds, fd, &f) != 0)
        return SYS_RES(0, -EBADF);

    Attributes attr;
    int res = file_stat(f, &attr);
    if (res < 0)
        return SYS_RES(0, res);

    struct stat s = (struct stat) {
        .st_dev     = 0,              // device ID (fake/default)
        .st_ino     = 0,              // inode number (if no real FS)
        .st_nlink   = 1,              // default link count
        .st_mode    = 0,              // file type + permissions
        .st_uid     = 0,              // root
        .st_gid     = 0,              // root group
        .__pad0     = 0,
        .st_rdev    = 0,              // device ID (for char/block devs)
        .st_size    = attr.size,     // actual file size
        .st_blksize = 4096,           // common default block size
        .st_blocks  = (attr.size + 511) / 512, // count in 512-byte blocks
        .st_atim    = {0, 0},         // access time — stub
        .st_mtim    = {0, 0},         // modify time — stub
        .st_ctim    = {0, 0},         // change time — stub
        .__unused   = {0, 0, 0}
    };

    copy_buffer_to_user(statbuf, &s, sizeof(struct stat));

    return SYS_RES(0, 0);
}
