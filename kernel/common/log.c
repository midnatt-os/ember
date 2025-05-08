#include "common/log.h"
#include "cpu/port.h"

#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0

#define NANOPRINTF_IMPLEMENTATION
#include <nanoprintf.h>

#include "common/lock/spinlock.h"
#include "sys/time.h"

#define RESET_COLOR "\033[0m"
#define INFO_COLOR  "\033[35m"  // Magenta
#define DEBUG_COLOR "\033[39m"  // Default
#define WARN_COLOR  "\033[33m"  // Yellow
#define ERROR_COLOR "\033[31m"  // Red



extern TimeSource* time_source;

Spinlock log_lock = SPINLOCK_NEW;

static void qemu_dbg_putc(char c) {
    port_outb(0xE9, c);
}

static void qemu_dbg_puts(const char* str) {
    while (*str)
        qemu_dbg_putc(*str++);
}

void log_list(LogLevel level, const char* tag, const char* fmt, va_list list) {
    char* level_name = "DEBUG";
    char* color = DEBUG_COLOR;

    switch (level) {
        case LOG_INFO: level_name = "INFO"; color = INFO_COLOR; break;
        case LOG_DEBUG: level_name = "DEBUG"; color = DEBUG_COLOR; break;
        case LOG_WARN: level_name = "WARN"; color = WARN_COLOR; break;
        case LOG_ERROR: level_name = "ERROR"; color = ERROR_COLOR; break;
    }

    uint64_t now = time_source != nullptr ? time_current() : 0;
    uint64_t seconds = ns_to_s(now);
    uint64_t milliseconds = ns_to_ms(now) % 1000;

    char buffer[512];
    int prefix_len = npf_snprintf(buffer, sizeof(buffer), "[%lu:%03lu] [%s] %s: ", seconds, milliseconds, level_name, tag);
    npf_vsnprintf(buffer + prefix_len, sizeof(buffer) - prefix_len, fmt, list);

    qemu_dbg_puts(color);
    qemu_dbg_puts(buffer);
    qemu_dbg_puts(RESET_COLOR);
}

void log(LogLevel level, const char* tag, const char* fmt, ...) {
    spinlock_acquire(&log_lock);
    va_list args;
    va_start(args, fmt);
    log_list(level, tag, fmt, args);
    va_end(args);
    spinlock_release(&log_lock);
}

void logln(LogLevel level, const char* tag, const char* fmt, ...) {
    spinlock_acquire(&log_lock);
    va_list args;
    va_start(args, fmt);
    log_list(level, tag, fmt, args);
    qemu_dbg_putc('\n');
    va_end(args);
    spinlock_release(&log_lock);
}

void log_raw(const char* fmt, ...) {
    spinlock_acquire(&log_lock);
    va_list list;
    va_start(list, fmt);
    char buffer[512];
    npf_vsnprintf(buffer, sizeof(buffer), fmt, list);
    qemu_dbg_puts(buffer);
    va_end(list);
    spinlock_release(&log_lock);
}
