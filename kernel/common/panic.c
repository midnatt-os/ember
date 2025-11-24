#include "common/panic.h"

#include "common/asm.h"
#include "common/limine_requests.h"
#include "common/log.h"
#include "common/stack_trace.h"
#include "cpu/cpu.h"
#include "cpu/interrupts.h"
#include "cpu/lapic.h"
#include "limine.h"
#include "stdarg.h"

#include <stdatomic.h>
#include <stdint.h>

#define NMI_VECTOR 0x2

bool panic_in_progress = false;
uint64_t panic_ack_count = 0;

void panic(const char* fmt, ...) {
    lapic_broadcast_ipi((uint8_t) NMI_VECTOR, LAPIC_DM_NMI, false);
    size_t expected = mp_request.response->cpu_count - 1;
    while (atomic_load_explicit(&panic_ack_count, memory_order_acquire) < expected)
        relax();

    va_list list;
    va_start(list, fmt);

    atomic_store_explicit(&panic_in_progress, true, memory_order_release);
    log_raw("(CPU%d) ", CPU_CURRENT->seq_id);
    log_list(LOG_ERROR, "PANIC", fmt, list);
    log_raw("\n");
    log_stack_trace();

    va_end(list);

    while (true)
        halt();
}
