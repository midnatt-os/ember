#pragma once

#include "abi/termios.h"
#include "lib/ringbuf.h"
#include "sched/thread.h"

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

typedef struct {
    RingBuffer* in;
    RingBuffer* out;

    struct termios termios;
    struct winsize winsize;

    Thread* read_wait;
    Thread* write_wait;
} Tty;

void tty_init();
