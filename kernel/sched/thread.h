#pragma once

#include "events/event.h"
#include "lib/list.h"
#include "sched/process.h"

#include <stdint.h>

typedef struct Thread Thread;

typedef struct {
    uintptr_t base;
    uintptr_t size;
} ThreadStack;

typedef struct [[gnu::packed]] {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    void (*thread_init)(Thread* prev);
    void (*entry)();
} KernelInitStack;

typedef enum {
    STATUS_READY,
    STATUS_RUNNING,
    STATUS_DONE,
} ThreadStatus;

typedef struct Thread {
    uintptr_t rsp;
    ThreadStack kernel_stack;

    uint64_t tid;
    char name[16];
    Process* proc;
    ThreadStatus status;

    Event* event;
    ListNode proc_list_node;
    ListNode sched_list_node;
} Thread;

extern _Atomic uint64_t next_tid;

Thread* thread_kernel_create(void (*entry)(), const char* name);
