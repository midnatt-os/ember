#pragma once

#include "lib/list.h"
#include "mem/slab.h"
#include "sys/timers.h"

#include <stdint.h>

typedef enum {
    STATUS_READY,
    STATUS_RUNNING,
    STATUS_BLOCKED,
    STATUS_DONE,
} thread_status_t;

typedef struct {
    uintptr_t rsp;

    uint64_t tid;
    char* name;
    thread_status_t status;
    uint32_t cpu_id;

    list_node_t sched_list_node;
    list_node_t reap_list_node;

    void* kstack_base;
    size_t kstack_size;

    timer_t sleep_timer;
    list_node_t wq_node;
} thread_t;

extern object_cache_t* thread_cache;

void thread_exit();
void thread_destroy(thread_t* thread);

thread_t* thread_create_kernel(char* name, void* entry_fn);
