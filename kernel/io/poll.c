#include "io/poll.h"

#include "cpu/tsc.h"
#include "fs/fd.h"
#include "fs/file.h"
#include "lib/container.h"
#include "mem/heap.h"
#include "sched/sched.h"
#include "sched/thread.h"

typedef struct poll_entry {
    wait_queue_t* wq;
    wait_node_t wn;
    struct poll_entry* next;
} poll_entry_t;

typedef struct {
    poll_table_t pt;
    poll_entry_t* entries;
    thread_t* t;
} poll_ctx_t;

static void poll_qproc(poll_table_t* pt, wait_queue_t* wq) {
    poll_ctx_t* ctx = (poll_ctx_t*) pt->priv;

    poll_entry_t* e = (poll_entry_t*) heap_alloc(sizeof(*e));

    e->wq = wq;
    e->next = ctx->entries;
    ctx->entries = e;

    wait_node_init(&e->wn, ctx->t);
    waitq_add(wq, &e->wn);
}

static void poll_cleanup(poll_ctx_t* ctx) {
    poll_entry_t* e = ctx->entries;
    while (e) {
        poll_entry_t* next = e->next;
        waitq_remove(e->wq, &e->wn);
        heap_free(e, sizeof(*e));
        e = next;
    }
    ctx->entries = nullptr;
}

int io_ppoll(pollfd_t* kfds, size_t nfds, const timespec_t* timeout) {
    const poll_mask_t ALWAYS = (POLLERR | POLLHUP | POLLNVAL);

    const bool infinite = (timeout == nullptr);
    const uint64_t timeout_ns = infinite ? 0 : timespec_to_ns(timeout);

    uint64_t deadline = 0;
    if (!infinite)
        deadline = tsc_time() + timeout_ns;

    for (;;) {
        poll_ctx_t ctx = {
            .pt = {.qproc = poll_qproc, .priv = nullptr},
            .entries = nullptr,
            .t = sched_get_current_thread(),
        };
        ctx.pt.priv = &ctx;

        int ready = 0;

        for (size_t i = 0; i < nfds; i++) {
            kfds[i].revents = 0;

            if (kfds[i].fd < 0)
                continue;

            file_t* f = fd_get(ctx.t->proc->fd_table, kfds[i].fd);
            if (!f) {
                kfds[i].revents = POLLNVAL;
                ready++;
                continue;
            }

            poll_mask_t m = file_poll(f, &ctx.pt);
            file_put(f);

            poll_mask_t want = (poll_mask_t) kfds[i].events | ALWAYS;
            poll_mask_t re = m & want;

            kfds[i].revents = (int16_t) re;
            if (re)
                ready++;
        }

        if (ready > 0) {
            poll_cleanup(&ctx);
            return ready;
        }

        if (!infinite) {
            if (timeout_ns == 0) {
                poll_cleanup(&ctx);
                return 0;
            }

            uint64_t now = tsc_time();
            if (now >= deadline) {
                poll_cleanup(&ctx);
                return 0;
            }

            uint64_t remaining = deadline - now;

            // IMPORTANT: keep registrations while sleeping.
            // sched_sleep must be wakeable via sched_wake_thread().
            sched_sleep(remaining);

            // We woke either because an FD event called sched_wake_thread(),
            // or because the timer expired. Either way: cleanup + rescan.
            poll_cleanup(&ctx);
            continue;
        }

        // infinite: block until some wake happens, then cleanup + rescan
        sched_yield(STATUS_BLOCKED);
        poll_cleanup(&ctx);
        // loop
    }
}
