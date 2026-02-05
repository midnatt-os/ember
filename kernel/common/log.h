#pragma once

#include <stdarg.h>
#include <stddef.h>

typedef enum {
    LOG_INFO,
    LOG_DEBUG,
    LOG_WARN,
    LOG_ERROR,
} LogLevel;

void log_list(LogLevel level, const char* tag, const char* fmt, va_list list);
void log(LogLevel level, const char* tag, const char* fmt, ...);
void logln(LogLevel level, const char* tag, const char* fmt, ...);
void log_raw(const char* fmt, ...);
void log_console_write(const char* buf, size_t count);
void log_init();
