#include "common/panic.h"

#include "common/asm.h"
#include "common/log.h"
#include "common/stack_trace.h"
#include "stdarg.h"

void panic(const char *fmt, ...) {
  va_list list;
  va_start(list, fmt);
  log_list(LOG_ERROR, "PANIC", fmt, list);
  log_raw("\n");
  log_raw("TRYING STACKTRACE\n");
  log_stack_trace();
  va_end(list);
  halt();
}
