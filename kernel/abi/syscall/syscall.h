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
#define SYSCALL_MMAP 13
#define SYSCALL_MPROTECT 14
#define SYSCALL_MKDIR 15
#define SYSCALL_DUP2 16
#define SYSCALL_GETTIME 17
#define SYSCALL_NSLEEP 18
#define SYSCALL_GETPID 19
#define SYSCALL_GETCWD 20
#define SYSCALL_ISATTY 21
#define SYSCALL_GETPPID 22
#define SYSCALL_IOCTL 23
#define SYSCALL_FCNTL 24
#define SYSCALL_STAT 25
#define SYSCALL_DUP 26

typedef struct {
    uint64_t value;
    int error;
} SyscallResult;
