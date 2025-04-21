#include "common/panic.h"

#include "stdarg.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "sys/stack_trace.h"


[[noreturn]] void panic(const char *fmt, ...) {
    va_list list;
    va_start(list, fmt);
    log_list(LOG_ERROR, "PANIC", fmt, list);
    log_stack_trace();
    va_end(list);
    cpu_halt();
}
