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

typedef enum {
    STATUS_READY,
    STATUS_RUNNING,
    STATUS_BLOCKED,
    STATUS_DONE,
} ThreadStatus;

typedef struct Thread {
    uintptr_t rsp;
    uintptr_t syscall_rsp;
    ThreadStack kernel_stack;

    void* fpu_state;

    uint64_t tid;
    char name[16];
    Process* proc;
    ThreadStatus status;

    Event* event;
    ListNode proc_list_node;
    ListNode sched_list_node;
    ListNode wait_list_node;
} Thread;

extern _Atomic uint64_t next_tid;

Thread* thread_kernel_create(void (*entry)(), const char* name);
Thread* thread_create_user(Process* proc, const char* name, uintptr_t entry_rip, uintptr_t user_sp);
