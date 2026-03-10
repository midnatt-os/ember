#include "tty.h"

#include "common/assert.h"
#include "common/limine_requests.h"
#include "common/log.h"
#include "flanterm.h"
#include "fs/impl/devfs.h"
#include "lib/mem.h"
#include "mem/heap.h"
#include "mem/page.h"
#include "mem/vm.h"
#include "sched/proc.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "sys/syscalls/syscall.h"

#include <stddef.h>
#include <stdint.h>

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TCGETA 0x5405
#define TCSETA 0x5406
#define TCSETAW 0x5407
#define TCSETAF 0x5408
#define TCSBRK 0x5409
#define TCXONC 0x540A
#define TCFLSH 0x540B
#define TIOCEXCL 0x540C
#define TIOCNXCL 0x540D
#define TIOCSCTTY 0x540E
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCOUTQ 0x5411
#define TIOCSTI 0x5412
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414


extern struct flanterm_context* ft_ctx;

static tty_t g_tty0;

static inline const char* tty_ioctl_name(unsigned long req) {
    switch ((uint32_t) req) {
        /* termios */
        case 0x5401: return "TCGETS";
        case 0x5402: return "TCSETS";
        case 0x5403: return "TCSETSW";
        case 0x5404: return "TCSETSF";

        /* termio (old) */
        case 0x5405: return "TCGETA";
        case 0x5406: return "TCSETA";
        case 0x5407: return "TCSETAW";
        case 0x5408: return "TCSETAF";

        /* flow / break / flush */
        case 0x5409: return "TCSBRK";
        case 0x540A: return "TCXONC";
        case 0x540B: return "TCFLSH";
        case 0x5425:
            return "TCSBRKP"; /* POSIX tcsendbreak() */

        /* exclusive / controlling tty */
        case 0x540C: return "TIOCEXCL";
        case 0x540D: return "TIOCNXCL";
        case 0x540E: return "TIOCSCTTY";
        case 0x5422: return "TIOCNOTTY";

        /* job control */
        case 0x540F: return "TIOCGPGRP";
        case 0x5410: return "TIOCSPGRP";
        case 0x5429: return "TIOCGSID";

        /* queues / inject */
        case 0x5411: return "TIOCOUTQ";
        case 0x5412: return "TIOCSTI";

        /* window size */
        case 0x5413: return "TIOCGWINSZ";
        case 0x5414: return "TIOCSWINSZ";

        /* line discipline */
        case 0x5423: return "TIOCSETD";
        case 0x5424: return "TIOCGETD";

        /* Linux-specific / console redirect */
        case 0x541C: return "TIOCLINUX";
        case 0x541D: return "TIOCCONS";

        /* pty helpers */
        case 0x5420:
            return "TIOCPKT"; /* enable/disable packet mode */
        /* NOTE: TIOCPKT_* are NOT ioctls; they are status bits returned in packet mode. */

        /* Classic “bytes available” on tty is commonly issued as TIOCINQ,
           but numerically it’s the same as the generic FIONREAD (0x541B). */
        case 0x541B:
            return "TIOCINQ"; /* aka FIONREAD */

        /* SYSV compatibility */
        case 0x5432: return "TCGETX";
        case 0x5433: return "TCSETX";
        case 0x5434: return "TCSETXF";
        case 0x5435: return "TCSETXW";

        default: return "UNKNOWN_TTY_IOCTL";
    }
}

static void tty_flush_input_locked(tty_t* tty) {
    tty->inq.r = tty->inq.w = tty->inq.len = 0;
}

static void rb_init(tty_ringbuf_t* rb) {
    rb->buf = vm_map_anon(&global_as, nullptr, PAGE_SIZE, 0, VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_DEFAULT);
    ASSERT(rb->buf != nullptr);
    rb->cap = PAGE_SIZE;
    rb->r = rb->w = rb->len = 0;
}

static size_t rb_push(tty_ringbuf_t* rb, const uint8_t* in, size_t n) {
    size_t pushed = 0;
    while (pushed < n && rb->len < rb->cap) {
        rb->buf[rb->w] = in[pushed++];
        rb->w = (rb->w + 1) % rb->cap;
        rb->len++;
    }
    return pushed;
}

static size_t rb_pop(tty_ringbuf_t* rb, uint8_t* out, size_t n) {
    size_t popped = 0;
    while (popped < n && rb->len > 0) {
        out[popped++] = rb->buf[rb->r];
        rb->r = (rb->r + 1) % rb->cap;
        rb->len--;
    }
    return popped;
}

void tty_inject(const uint8_t* data, size_t len) {
    mutex_lock(&g_tty0.lock);
    rb_push(&g_tty0.inq, data, len);
    mutex_unlock(&g_tty0.lock);
    waitq_wake_all(&g_tty0.read_wq);
}

static int tty_open(devfs_node_t* node, uint32_t flags, vnode_t** out_vn) {
    (void) flags;

    // Example: reserve minor 255 as "/dev/tty" (ctty alias)
    if (node->minor == 255) {
        process_t* p = sched_get_current_thread()->proc;

        // For now, you might just return tty0 if you don’t have sessions yet:
        if (!p->ctty_vnode)
            return -ENXIO;

        *out_vn = p->ctty_vnode;
        return 0;
    }

    // Normal case: open tty0/tty1/...
    *out_vn = node->vnode;
    return 0;
}

ssize_t tty_read(void* buffer, size_t count, off_t _) {
    while (true) {
        mutex_lock(&g_tty0.lock);
        if (g_tty0.inq.len > 0) {
            size_t n = rb_pop(&g_tty0.inq, buffer, count);
            mutex_unlock(&g_tty0.lock);
            return (ssize_t) n;
        }

        mutex_unlock(&g_tty0.lock);

        wait_node_t wn;
        wait_node_init(&wn, sched_get_current_thread());
        waitq_add(&g_tty0.read_wq, &wn);

        sched_yield(STATUS_BLOCKED);

        waitq_remove(&g_tty0.read_wq, &wn);
    }
}

ssize_t tty_write(const void* buffer, size_t count, off_t offset) {
    (void) offset;
    if (!buffer)
        return -EINVAL;

    mutex_lock(&g_tty0.lock);
    bool onlcr = (g_tty0.tio.c_oflag & OPOST) && (g_tty0.tio.c_oflag & ONLCR);
    mutex_unlock(&g_tty0.lock);

    const char* s = buffer;
    for (size_t i = 0; i < count; i++) {
        char ch = s[i];
        if (onlcr && ch == '\n')
            log_raw("\r");
        log_raw("%c", ch);
    }

    return (ssize_t) count;
}


static int tty_ioctl(devfs_node_t* node, uint64_t req, uintptr_t u_arg) {
    (void) node;
    process_t* proc = sched_get_current_thread()->proc;

    logln(LOG_DEBUG, "TTY_IOCTL", "%s", tty_ioctl_name(req));

    switch (req) {
        case TCGETS: {
            if (!u_arg)
                return -EFAULT;
            ktermios_t t;
            mutex_lock(&g_tty0.lock);
            t = g_tty0.tio;
            mutex_unlock(&g_tty0.lock);
            if (copy_to_user(u_arg, &t, sizeof(t), proc->address_space) < 0)
                return -EFAULT;
            return 0;
        }

        case TCSETS:
        case TCSETSW:
        case TCSETSF: {
            if (!u_arg)
                return -EFAULT;
            ktermios_t newt;
            if (copy_from_user(&newt, u_arg, sizeof(newt), proc->address_space) < 0)
                return -EFAULT;

            mutex_lock(&g_tty0.lock);
            g_tty0.tio = newt;

            if (req == TCSETSF)
                tty_flush_input_locked(&g_tty0);

            // TCSETSW would “drain output”; you have no output queue yet, so same as TCSETS.
            mutex_unlock(&g_tty0.lock);
            return 0;
        }

        case TIOCGWINSZ: {
            if (!u_arg)
                return -EFAULT;
            winsize_t ws;
            mutex_lock(&g_tty0.lock);
            ws = g_tty0.ws;
            mutex_unlock(&g_tty0.lock);
            if (copy_to_user(u_arg, &ws, sizeof(ws), proc->address_space) < 0)
                return -EFAULT;
            return 0;
        }

        case TIOCSWINSZ: {
            if (!u_arg)
                return -EFAULT;
            winsize_t new_ws;
            if (copy_from_user(&new_ws, u_arg, sizeof(new_ws), proc->address_space) < 0)
                return -EFAULT;

            mutex_lock(&g_tty0.lock);
            g_tty0.ws = new_ws;
            mutex_unlock(&g_tty0.lock);

            // TODO later: SIGWINCH to foreground pgrp
            return 0;
        }

        default: return -ENOTTY;
    }
}

static poll_mask_t tty_poll(devfs_node_t* _, poll_table_t* pt) {
    poll_mask_t m = POLLOUT;

    mutex_lock(&g_tty0.lock);
    poll_wait(pt, &g_tty0.read_wq);

    if (g_tty0.inq.len > 0)
        m |= POLLIN;

    mutex_unlock(&g_tty0.lock);

    return m;
}

static dev_ops_t tty_ops = {.open = tty_open, .read = tty_read, .write = tty_write, .ioctl = tty_ioctl, .poll = tty_poll};

void tty_init() {
    // choose a major; doesn't matter yet as long as consistent
    const int TTY_MAJOR = 4;

    g_tty0.lock = MUTEX_NEW;
    rb_init(&g_tty0.inq);
    waitq_init(&g_tty0.read_wq);

    struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
    size_t rows;
    size_t cols;

    flanterm_get_dimensions(ft_ctx, &cols, &rows);

    g_tty0.ws = (winsize_t) {
        .row = (uint16_t) rows,
        .col = (uint16_t) cols,
        .xpixel = fb->width,
        .ypixel = fb->height,
    };

    ktermios_t t = {0};

    t.c_iflag = ICRNL | IXON;
    t.c_oflag = OPOST | ONLCR;
    t.c_cflag = 0; // you can ignore for now
    t.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;

    // Common defaults
    t.c_cc[VINTR] = 3; // ^C
    t.c_cc[VQUIT] = 28; // ^backslash
    t.c_cc[VERASE] = 127; // DEL (sometimes 8)
    t.c_cc[VKILL] = 21; // ^U
    t.c_cc[VEOF] = 4; // ^D
    t.c_cc[VSTART] = 17; // ^Q
    t.c_cc[VSTOP] = 19; // ^S
    t.c_cc[VSUSP] = 26; // ^Z

    // Non-canonical read defaults (readline often sets these itself)
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;

    g_tty0.tio = t;

    ASSERT(devfs_make_node("/tty0", V_CHR, TTY_MAJOR, 0, &tty_ops) == 0);
    ASSERT(devfs_make_node("/tty", V_CHR, TTY_MAJOR, 255, &tty_ops) == 0);
}
