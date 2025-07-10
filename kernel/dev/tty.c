#include "dev/tty.h"

#include <stddef.h>
#include <stdint.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>

#include "common/log.h"
#include "fs/devfs/devfs.h"
#include "fs/vnode.h"
#include "memory/heap.h"
#include "abi/termios.h"
#include "abi/errno.h"
#include "lib/ringbuf.h"
#include "lib/mem.h"
#include "memory/hhdm.h"
#include "sys/framebuffer.h"
#include "syscall/syscall.h"



Tty* g_tty;
struct flanterm_context* ft_ctx;

static const struct termios tty_default_termios = {
    .c_iflag = BRKINT | ICRNL  | IXON,
    .c_oflag = OPOST  | ONLCR,
    .c_cflag = CREAD   | CS8   | HUPCL,
    .c_lflag = ISIG    | ICANON | ECHO | ECHOE | ECHOK,
    .c_line  = 0,
    .c_cc = {
        [VINTR]   = 3,    // Ctrl-C
        [VQUIT]   = 28,   // Ctrl- backslash
        [VERASE]  = 127,  // DEL
        [VKILL]   = 21,   // Ctrl-U
        [VEOF]    = 4,    // Ctrl-D
        [VTIME]   = 0,
        [VMIN]    = 1,
        [VSTART]  = 17,   // Ctrl-Q
        [VSTOP]   = 19,   // Ctrl-S
        [VSUSP]   = 26,   // Ctrl-Z
        [VREPRINT]= 18,   // Ctrl-R
        [VDISCARD]= 15,   // Ctrl-O
        [VWERASE] = 23,   // Ctrl-W
        [VLNEXT]  = 22,   // Ctrl-V
        [VEOL]    = 0,
        [VEOL2]   = 0,
    }
};

static const struct winsize tty_default_winsize = {
    .ws_row    = 25,
    .ws_col    = 80,
    .ws_xpixel = 0,
    .ws_ypixel = 0,
};

void tty_flush_output(Tty* tty) {
    uint64_t val;
    while (ringbuf_pop(tty->out, &val)) {
        char c = (char) val;

        flanterm_write(ft_ctx, &c, 1);
    }
}

#define TCGETS		0x5401
#define TCSETS		0x5402
#define TIOCGWINSZ	0x5413
#define TIOCSWINSZ	0x5414
#define TIOCGPGRP 0x540F

int tty_ioctl(void* ctx, uint64_t request, void* argp) {
    Tty* tty = ctx;

    switch (request) {
        case TCGETS:
            memcpy(argp, &tty->termios, sizeof(struct termios)); return 0;

        case TCSETS:
            memcpy(&tty->termios, argp, sizeof(struct termios)); return 0;

        case TIOCGWINSZ:
            memcpy(argp, &tty->winsize, sizeof(struct winsize)); return 0;

        case TIOCSWINSZ:
            memcpy(&tty->winsize, argp, sizeof(struct winsize)); return 0;

        case TIOCGPGRP:
            uint8_t sus = 0;
            memcpy(argp, &sus, 1); return 0;

        default:
            return -ENOTTY;
    }
}

ssize_t tty_write(void* ctx, const void* buf, size_t len, [[maybe_unused]] off_t off) {
    Tty* tty = ctx;
    char* c_buf = (char*) buf;

    c_buf = copy_string_from_user((char*) c_buf, len);
    if (buf == nullptr)
        return -EFAULT;

    for (size_t i = 0; i < len; i++)
        ringbuf_push(tty->out, c_buf[i]);

    tty_flush_output(tty);

    return len;
}

ssize_t tty_read([[maybe_unused]] void* ctx, [[maybe_unused]] void* buf, [[maybe_unused]] size_t len, [[maybe_unused]] off_t off) {
    char* test = "1234";
    memcpy(buf, test, 5);
    return 5;
}


DeviceOps tty_devops = {
    .ioctl = tty_ioctl,
    .write = tty_write,
    .read = tty_read,
};

void tty_init() {
    ft_ctx = flanterm_fb_init(
        NULL,
        NULL,
        HHDM(fb.address), fb.width, fb.height, fb.pitch,
        fb.red_mask_size, fb.red_mask_shift,
        fb.green_mask_size, fb.green_mask_shift,
        fb.blue_mask_size, fb.blue_mask_shift,
        NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, 0, 0, 1,
        0, 0,
        0
    );

    g_tty = kmalloc(sizeof(Tty));

    g_tty->in = ringbuf_new(4096);
    g_tty->out = ringbuf_new(4096);

    memcpy(&g_tty->termios, &tty_default_termios, sizeof(struct termios));
    memcpy(&g_tty->winsize, &tty_default_winsize,  sizeof(struct winsize));

    devfs_register("/dev", "tty", VNODE_TYPE_CHAR_DEV, &tty_devops, g_tty);
    logln(LOG_INFO, "TTY", "Initialized");
}
