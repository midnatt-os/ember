#pragma once

#include <stdint.h>

#define SYSCALL_EXIT 0
#define SYSCALL_DEBUG 1
#define SYSCALL_SET_TCB 2
#define SYSCALL_ANON_ALLOC 3
#define SYSCALL_ANON_FREE 4
#define SYSCALL_OPEN 5
#define SYSCALL_CLOSE 6
#define SYSCALL_READ 7
#define SYSCALL_WRITE 8
#define SYSCALL_SEEK 9
#define SYSCALL_FETCH_FRAMEBUFFER 10
#define SYSCALL_FORK 11
#define SYSCALL_EXECVE 12

typedef struct {
    uint64_t value;
    int error;
} SyscallResult;
