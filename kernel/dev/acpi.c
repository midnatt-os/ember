#include <uacpi/uacpi.h>

#include "common/log.h"
#include "memory/heap.h"
#include "memory/hhdm.h"



uintptr_t _rsdp_address;

void acpi_init(uintptr_t rsdp_address) {
    _rsdp_address = rsdp_address;

    void* tmp_buffer = kmalloc(4096);
    uacpi_setup_early_table_access(tmp_buffer, 4096);
}

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    *out_rsdp_address = PHYS_FROM_HHDM(_rsdp_address);
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_map(uacpi_phys_addr addr, [[maybe_unused]] uacpi_size len) {
    return (void*) HHDM(addr);
}

void uacpi_kernel_unmap([[maybe_unused]] void* addr, [[maybe_unused]] uacpi_size len) {}


UACPI_PRINTF_DECL(2, 3)
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *fmt, ...) {
    va_list list;
    va_start(list, fmt);

    switch (level) {
        case UACPI_LOG_TRACE:
        case UACPI_LOG_DEBUG: break;
        case UACPI_LOG_INFO: log_list(LOG_INFO, "UACPI", fmt, list); break;
        case UACPI_LOG_WARN: log_list(LOG_WARN, "UACPI", fmt, list); break;
        case UACPI_LOG_ERROR: log_list(LOG_ERROR, "UACPI", fmt, list); break;
    }

    va_end(list);
}
