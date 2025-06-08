#include "syscall/syscall.h"

#include <stddef.h>
#include <stdint.h>

#include "abi/syscall/syscall.h"
#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/registers.h"
#include "fs/vfs.h"
#include "memory/heap.h"
#include "memory/vm.h"
#include "sched/process.h"
#include "sched/sched.h"
#include "sched/thread.h"

#define MSR_EFER_SCE (1 << 0)

#define MAX_DEBUG_STR_LEN 512



extern void syscall_entry();

void* copy_buffer_from_user(void* src, size_t length) {
    void* buffer = kmalloc(length);

    VmAddressSpace* as = sched_get_current_process()->as;
    size_t bytes_read = vm_copy_from(buffer, as, (uintptr_t) src, length);

    if (bytes_read != length) {
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
        res.error = SYSCALL_ERR_INVALID_VALUE;
        return res;
    }

    logln(LOG_DEBUG, "SYSCALL_DEBUG", "%s", str);

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
        res.error = SYSCALL_ERR_INVALID_VALUE;
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
        res.error = SYSCALL_ERR_INVALID_VALUE;
        return res;
    }

    VmAddressSpace* as = sched_get_current_process()->as;

    vm_unmap(as, ptr, size);

    logln(LOG_DEBUG, "SYSCALL", "anon_free(ptr: %#p, size: %#lx)", ptr, size);

    return res;
}

SyscallResult syscall_open(char* path, [[maybe_unused]]size_t path_length, int flags, [[maybe_unused]] unsigned int mode) {
    logln(LOG_DEBUG, "SYSCALL", "open(path: %s, flags: %d, mode: %o)", path, flags, mode);
    /*
    SyscallResult res = {};
    Process* proc = sched_get_current_process();

    path = copy_string_from_user(path, path_length);
    if (path == nullptr) {
        res.error = SYSCALL_ERR_INVALID_VALUE;
        return res;
    }

    VNode* node;
    if (vfs_lookup(path, &node) != VFS_RES_OK) {
        res.error = SYSCALL_ERR_INVALID_VALUE; // TODO: file not found etc.
        return res;
    }

    File* f = file_create(node, flags);

    int fd = fd_alloc(&proc->fds);
    if (fd == -1) {
        res.error = SYSCALL_ERR_INVALID_VALUE; // TODO
        return res;
    }

    fd_set(&proc->fds, fd, f);*/

    return (SyscallResult) { .value = 0, .error = SYSCALL_ERR_NONE };
}
