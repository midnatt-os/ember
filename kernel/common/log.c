#include "common/log.h"

#include "common/asm.h"
#include "common/limine_requests.h"
#include "common/lock/spinlock.h"
#include "flanterm.h"
#include "flanterm_backends/fb.h"
#include "sys/time.h"

#include <stdint.h>

#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_ALT_FORM_FLAG 1
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS 1

#define NANOPRINTF_IMPLEMENTATION
#include "nanoprintf.h"

#define RESET_COLOR "\033[0m"
#define INFO_COLOR "\033[35m" // Magenta
#define DEBUG_COLOR "\033[39m" // Default
#define WARN_COLOR "\033[33m" // Yellow
#define ERROR_COLOR "\033[31m" // Red


struct flanterm_context* ft_ctx = nullptr;
spinlock_t log_lock = SPINLOCK_NEW;

extern bool panic_in_progress;

static inline bool log_lock_acquire(bool* taken) {
    if (__atomic_load_n(&panic_in_progress, __ATOMIC_RELAXED)) {
        *taken = false;
        return false;
    }
    *taken = true;
    return spinlock_lock(&log_lock);
}

static inline void log_lock_release(bool taken, bool prev) {
    if (!taken)
        return;
    spinlock_unlock(&log_lock, prev);
}


uint64_t get_time_zero() {
    return 0;
}

uint64_t (*log_get_time)() = get_time_zero;

[[maybe_unused]] static void qemu_dbg_putc(char c) {
    outb(0xE9, c);
}

[[maybe_unused]] static void qemu_dbg_puts(const char* str) {
    while (*str)
        qemu_dbg_putc(*str++);
}

[[maybe_unused]] static void fb_putc(char c) {
    flanterm_write(ft_ctx, &c, sizeof(char));
}

[[maybe_unused]] static void fb_puts(const char* str) {
    while (*str)
        fb_putc(*str++);
}

static void log_putc(char c) {
#ifdef LOGGING_SERIAL
    qemu_dbg_putc(c);
#endif
#ifdef LOGGING_FB
    fb_putc(c);
#endif
}

static void log_puts(const char* str) {
#ifdef LOGGING_SERIAL
    qemu_dbg_puts(str);
#endif
#ifdef LOGGING_FB
    fb_puts(str);
#endif
}

void log_list(LogLevel level, const char* tag, const char* fmt, va_list list) {
    char* level_name = "DEBUG";
    char* color = DEBUG_COLOR;

    switch (level) {
        case LOG_INFO:
            level_name = "INFO";
            color = INFO_COLOR;
            break;
        case LOG_DEBUG:
            level_name = "DEBUG";
            color = DEBUG_COLOR;
            break;
        case LOG_WARN:
            level_name = "WARN";
            color = WARN_COLOR;
            break;
        case LOG_ERROR:
            level_name = "ERROR";
            color = ERROR_COLOR;
            break;
    }

    uint64_t now = log_get_time();
    uint64_t seconds = ns_to_s(now);
    uint64_t milliseconds = ns_to_ms(now) % 1000;

    char buffer[512];
    int prefix_len = npf_snprintf(buffer, sizeof(buffer), "[%lu:%03lu] [%s] %s: ", seconds, milliseconds, level_name, tag);
    npf_vsnprintf(buffer + prefix_len, sizeof(buffer) - prefix_len, fmt, list);

    log_puts(color);
    log_puts(buffer);
    log_puts(RESET_COLOR);
}

void log(LogLevel level, const char* tag, const char* fmt, ...) {
    bool taken;
    bool prev = log_lock_acquire(&taken);

    va_list args;
    va_start(args, fmt);
    log_list(level, tag, fmt, args);
    va_end(args);

    log_lock_release(taken, prev);
}

void logln(LogLevel level, const char* tag, const char* fmt, ...) {
    bool taken;
    bool prev = log_lock_acquire(&taken);

    va_list args;
    va_start(args, fmt);
    log_list(level, tag, fmt, args);
    log_putc('\n');
    va_end(args);

    log_lock_release(taken, prev);
}

void log_raw(const char* fmt, ...) {
    bool taken;
    bool prev = log_lock_acquire(&taken);

    va_list list;
    va_start(list, fmt);
    char buffer[512];
    npf_vsnprintf(buffer, sizeof(buffer), fmt, list);
    log_puts(buffer);
    va_end(list);

    log_lock_release(taken, prev);
}

void log_init() {
    struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
    ft_ctx = flanterm_fb_init(
        NULL,
        NULL,
        fb->address,
        fb->width,
        fb->height,
        fb->pitch,
        fb->red_mask_size,
        fb->red_mask_shift,
        fb->green_mask_size,
        fb->green_mask_shift,
        fb->blue_mask_size,
        fb->blue_mask_shift,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        0,
        0,
        1,
        0,
        0,
        0,
        FLANTERM_FB_ROTATE_0
    );
}
