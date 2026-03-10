#include "dev/ps2kb.h"

#include "common/log.h"
#include "common/panic.h"
#include "dev/ps2c.h"
#include "drivers/tty.h"
#include "lib/ringbuf.h"
#include "lib/string.h"
#include "mem/heap.h"
#include "sched/sched.h"
#include "sched/thread.h"

#include <stddef.h>
#include <stdint.h>

static thread_t* g_kbd_thread;

/* Set-2 prefix state */
static bool g_ext; // saw 0xE0
static bool g_break; // saw 0xF0

/* Modifiers */
static bool g_lshift;
static bool g_rshift;
static bool g_caps;

static bool g_lctrl;
static bool g_rctrl;
static bool g_lalt;
static bool g_ralt;


static inline bool shift_down(void) {
    return g_lshift || g_rshift;
}
static inline bool ctrl_down(void) {
    return g_lctrl || g_rctrl;
}
static inline bool alt_down(void) {
    return g_lalt || g_ralt;
}

static inline void emit_byte(uint8_t b) {
    tty_inject(&b, 1);
}

static void emit_str(const char* s) {
    tty_inject((const uint8_t*) s, strlen(s));
}

static char apply_shift(char c) {
    if (!shift_down())
        return c;

    switch (c) {
        case '1':  return '!';
        case '2':  return '@';
        case '3':  return '#';
        case '4':  return '$';
        case '5':  return '%';
        case '6':  return '^';
        case '7':  return '&';
        case '8':  return '*';
        case '9':  return '(';
        case '0':  return ')';
        case '-':  return '_';
        case '=':  return '+';
        case '[':  return '{';
        case ']':  return '}';
        case '\\': return '|';
        case ';':  return ':';
        case '\'': return '"';
        case ',':  return '<';
        case '.':  return '>';
        case '/':  return '?';
        case '`':  return '~';
        default:   return c;
    }
}

static char sc_set2_to_ascii(uint8_t code) {
    switch (code) {
        /* letters */
        case 0x1C: return 'a';
        case 0x32: return 'b';
        case 0x21: return 'c';
        case 0x23: return 'd';
        case 0x24: return 'e';
        case 0x2B: return 'f';
        case 0x34: return 'g';
        case 0x33: return 'h';
        case 0x43: return 'i';
        case 0x3B: return 'j';
        case 0x42: return 'k';
        case 0x4B: return 'l';
        case 0x3A: return 'm';
        case 0x31: return 'n';
        case 0x44: return 'o';
        case 0x4D: return 'p';
        case 0x15: return 'q';
        case 0x2D: return 'r';
        case 0x1B: return 's';
        case 0x2C: return 't';
        case 0x3C: return 'u';
        case 0x2A: return 'v';
        case 0x1D: return 'w';
        case 0x22: return 'x';
        case 0x35: return 'y';
        case 0x1A: return 'z';

        /* numbers */
        case 0x16: return '1';
        case 0x1E: return '2';
        case 0x26: return '3';
        case 0x25: return '4';
        case 0x2E: return '5';
        case 0x36: return '6';
        case 0x3D: return '7';
        case 0x3E: return '8';
        case 0x46: return '9';
        case 0x45: return '0';

        /* punctuation */
        case 0x0E: return '`';
        case 0x4E: return '-';
        case 0x55: return '=';
        case 0x54: return '[';
        case 0x5B: return ']';
        case 0x5D: return '\\';
        case 0x4C: return ';';
        case 0x52: return '\'';
        case 0x41: return ',';
        case 0x49: return '.';
        case 0x4A: return '/';

        /* whitespace/control */
        case 0x29: return ' '; // space
        case 0x5A: return '\n'; // enter
        case 0x66: return '\b'; // backspace (you can switch to 0x7f if you prefer)
        case 0x0D: return '\t'; // tab
        default:   return 0;
    }
}

static void emit_ansi_for_ext(uint8_t code) {
    /* Common Set-2 E0 make codes */
    switch (code) {
        case 0x75: emit_str("\x1b[A"); break; // Up
        case 0x72: emit_str("\x1b[B"); break; // Down
        case 0x74: emit_str("\x1b[C"); break; // Right
        case 0x6B: emit_str("\x1b[D"); break; // Left

        case 0x6C: emit_str("\x1b[H"); break; // Home
        case 0x69: emit_str("\x1b[F"); break; // End

        case 0x7D: emit_str("\x1b[5~"); break; // Page Up
        case 0x7A: emit_str("\x1b[6~"); break; // Page Down

        case 0x70: emit_str("\x1b[2~"); break; // Insert
        case 0x71: emit_str("\x1b[3~"); break; // Delete
        default:   break;
    }
}

static void handle_make_break(uint8_t code, bool is_break) {
    /* modifier keys (set 2) */
    if (!g_ext) {
        if (code == 0x12) {
            g_lshift = !is_break;
            return;
        } // LShift
        if (code == 0x59) {
            g_rshift = !is_break;
            return;
        } // RShift
        if (code == 0x14) {
            g_lctrl = !is_break;
            return;
        } // LCtrl
        if (code == 0x11) {
            g_lalt = !is_break;
            return;
        } // LAlt
        if (code == 0x58 && !is_break) {
            g_caps = !g_caps;
            return;
        } // Caps toggles on press
    } else {
        /* extended modifiers */
        if (code == 0x14) {
            g_rctrl = !is_break;
            return;
        } // E0 14
        if (code == 0x11) {
            g_ralt = !is_break;
            return;
        } // E0 11 (AltGr-ish)
    }

    /* only generate input on press */
    if (is_break)
        return;

    /* extended keys (arrows, etc.) */
    if (g_ext) {
        /* Alt+Arrow in terminals is often ESC ESC [ A, but many apps only need ESC [ A.
           If you want "Meta+arrow" semantics, prefix ESC when alt is held. */
        if (alt_down())
            emit_byte(0x1b);
        emit_ansi_for_ext(code);
        return;
    }

    char c = sc_set2_to_ascii(code);
    if (!c)
        return;

    /* shift/caps casing */
    if (c >= 'a' && c <= 'z') {
        bool upper = shift_down() ^ g_caps;
        if (upper)
            c = (char) ('A' + (c - 'a'));
    } else {
        c = apply_shift(c);
    }

    /* Ctrl+letter -> control codes */
    if (ctrl_down()) {
        char lower = c;
        if (lower >= 'A' && lower <= 'Z')
            lower = (char) ('a' + (lower - 'A'));

        if (lower >= 'a' && lower <= 'z') {
            emit_byte((uint8_t) (lower - 'a' + 1)); // ^A..^Z
            return;
        }
        /* optionally handle Ctrl+[ -> ESC etc later */
    }

    /* Alt as Meta prefix (ESC) */
    if (alt_down()) {
        emit_byte(0x1b);
    }

    emit_byte((uint8_t) c);
}

static void handle_scancode_byte(uint8_t b) {
    if (b == 0xE0) {
        g_ext = true;
        return;
    }
    if (b == 0xF0) {
        g_break = true;
        return;
    }

    handle_make_break(b, g_break);

    /* consume prefixes */
    g_ext = false;
    g_break = false;
}

static void ps2kb_thread_main(void) {
    while (true) {
        uint8_t b;
        bool did_work = false;

        while (ps2c_pop_kbd(&b)) {
            did_work = true;
            handle_scancode_byte(b);
        }

        if (!did_work)
            sched_yield(STATUS_BLOCKED);
    }
}

void ps2kb_notify_input(void) {
    sched_wake_thread(g_kbd_thread);
}

void ps2kb_init(void) {
    g_ext = g_break = false;
    g_lshift = g_rshift = false;
    g_caps = false;

    g_lctrl = g_rctrl = false;
    g_lalt = g_ralt = false;

    g_kbd_thread = thread_create_kernel("ps2kb", ps2kb_thread_main);

    sched_schedule_thread(g_kbd_thread);
    logln(LOG_INFO, "PS2KB", "Initialized");
}
