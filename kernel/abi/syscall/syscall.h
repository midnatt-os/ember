#pragma once

#include <stdint.h>

#define SYSCALL_EXIT 0
#define SYSCALL_DEBUG 1

typedef enum : uint64_t {
    SYSCALL_ERR_NONE = 0,
    SYSCALL_ERR_INVALID_VALUE,
} SyscallError;

typedef struct {
    uint64_t value;
    SyscallError error;
} SyscallResult;
