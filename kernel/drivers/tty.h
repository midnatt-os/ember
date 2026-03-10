#pragma once

#include "common/errno.h"
#include "common/lock/mutex.h"
#include "fs/vfs.h"
#include "sched/thread.h"
#include "sched/waitqueue.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t* buf;
    size_t cap;
    size_t r, w, len;
} tty_ringbuf_t;

typedef struct {
    uint16_t row;
    uint16_t col;
    uint16_t xpixel;
    uint16_t ypixel;
} winsize_t;

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

#define NCCS 32

typedef struct {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
    speed_t c_ibaud;
    speed_t c_obaud;
} ktermios_t;

// c_cc indices (Linux UAPI typical)
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSWTC 7
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT 15
#define VEOL2 16

// Flags you actually need early (use Linux UAPI values; these are octal)
#define ISIG 0000001
#define ICANON 0000002
#define ECHO 0000010
#define ECHOE 0000020
#define ECHOK 0000040
#define IEXTEN 0100000

#define ICRNL 0000400
#define IXON 0002000

#define OPOST 0000001
#define ONLCR 0000004

typedef struct tty {
    mutex_t lock;
    tty_ringbuf_t inq;

    wait_queue_t read_wq;

    winsize_t ws;
    ktermios_t tio;
} tty_t;

void tty_inject(const uint8_t* data, size_t len);

void tty_init();
