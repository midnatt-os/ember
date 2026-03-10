#pragma once

#include "common/errno.h"
#include "sched/waitqueue.h"
#include "sys/time.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    int16_t events;
    int16_t revents;
} pollfd_t;

#define POLLIN 0x0001
#define POLLPRI 0x0002
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020

// Keep this conservative at first to avoid huge kmalloc from user input
#define POLL_MAX_FDS 16 // TODO: ???

typedef uint32_t poll_mask_t;
typedef struct poll_table poll_table_t;
typedef void (*poll_qproc_t)(poll_table_t* pt, wait_queue_t* wq);

struct poll_table {
    poll_qproc_t qproc;
    void* priv;
};

static inline void poll_wait(poll_table_t* pt, wait_queue_t* wq) {
    if (pt && pt->qproc)
        pt->qproc(pt, wq);
}

int io_ppoll(pollfd_t* kfds, size_t nfds, const timespec_t* timeot);
