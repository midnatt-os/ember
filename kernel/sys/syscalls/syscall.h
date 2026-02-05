#pragma once

#include <stdint.h>

typedef struct {
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
} syscall_args_t;

typedef struct {
    uint64_t value;
    int error;
} syscall_result_t;

typedef syscall_result_t (*syscall_handler_t)(syscall_args_t* args);
