#include "sys/acpi.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/log.h"
#include "cpu/port.h"
#include "mem/heap.h"
#include "mem/hhdm.h"
#include "mem/page.h"
#include "mem/vm.h"

#include <stdint.h>
#include <uacpi/event.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>

typedef struct {
    uacpi_io_addr base;
    uacpi_size len;
} io_range_t;

void acpi_early_init() {
    void* tmp_buffer = vm_map_anon(&global_as, 0, PAGE_SIZE, 0, VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_DEFAULT);
    ASSERT(uacpi_setup_early_table_access(tmp_buffer, 4096) == UACPI_STATUS_OK);
}

void acpi_finalize_init() {
    ASSERT(uacpi_initialize(0) == UACPI_STATUS_OK);
    ASSERT(uacpi_namespace_load() == UACPI_STATUS_OK);
    ASSERT(uacpi_namespace_initialize() == UACPI_STATUS_OK);
    ASSERT(uacpi_finalize_gpe_initialization() == UACPI_STATUS_OK);
}

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    *out_rsdp_address = PHYS_FROM_HHDM((uintptr_t) rsdp_request.response->address);
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_map(uacpi_phys_addr addr, [[maybe_unused]] uacpi_size len) {
    size_t offset = addr % PAGE_SIZE;
    uintptr_t ret = (uintptr_t) vm_map_direct(&global_as, 0, ALIGN_UP(len + offset, PAGE_SIZE), 0, ALIGN_DOWN(addr, PAGE_SIZE), VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_DEFAULT);

    return (void*) (ret + offset);
}

void uacpi_kernel_unmap([[maybe_unused]] void* addr, [[maybe_unused]] uacpi_size len) {
    // logln(LOG_WARN, "UACPI", "uacpi_kernel_unmap() is stubbed");
}

UACPI_PRINTF_DECL(2, 3)
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* fmt, ...) {
    va_list list;
    va_start(list, fmt);

    switch (level) {
        case UACPI_LOG_TRACE:
        case UACPI_LOG_DEBUG:
        // case UACPI_LOG_INFO: break;
        case UACPI_LOG_INFO:  log_list(LOG_INFO, "UACPI", fmt, list); break;
        case UACPI_LOG_WARN:  log_list(LOG_WARN, "UACPI", fmt, list); break;
        case UACPI_LOG_ERROR: log_list(LOG_ERROR, "UACPI", fmt, list); break;
    }

    va_end(list);
}

uacpi_status uacpi_kernel_raw_memory_read(uacpi_phys_addr address, uacpi_u8 byte_width, uacpi_u64* out_value) {
    uint64_t virt = HHDM(address);

    switch (byte_width) {
        case 1:  *out_value = *(volatile uint8_t*) virt; break;
        case 2:  *out_value = *(volatile uint16_t*) virt; break;
        case 4:  *out_value = *(volatile uint32_t*) virt; break;
        case 8:  *out_value = *(volatile uint64_t*) virt; break;
        default: return UACPI_STATUS_INVALID_ARGUMENT;
    }

    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_raw_memory_write(uacpi_phys_addr address, uacpi_u8 byte_width, uacpi_u64 in_value) {
    uint64_t virt = HHDM(address);

    switch (byte_width) {
        case 1:  *(volatile uint8_t*) virt = in_value; break;
        case 2:  *(volatile uint16_t*) virt = in_value; break;
        case 4:  *(volatile uint32_t*) virt = in_value; break;
        case 8:  *(volatile uint64_t*) virt = in_value; break;
        default: return UACPI_STATUS_INVALID_ARGUMENT;
    }

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_raw_io_read([[maybe_unused]] uacpi_io_addr address, [[maybe_unused]] uacpi_u8 byte_width, [[maybe_unused]] uacpi_u64* out_value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_raw_io_write([[maybe_unused]] uacpi_io_addr address, [[maybe_unused]] uacpi_u8 byte_width, [[maybe_unused]] uacpi_u64 in_value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

void uacpi_kernel_pci_device_close([[maybe_unused]] uacpi_handle dev) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

uacpi_status uacpi_kernel_pci_read8([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u8* value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read16([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u16* value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read32([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u32* value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write8([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u8 value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write16([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u16 value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write32([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u32 value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_device_open([[maybe_unused]] uacpi_pci_address address, [[maybe_unused]] uacpi_handle* out_handle) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);

    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out_handle) {
    io_range_t* range = heap_alloc(sizeof(io_range_t));
    *range = (io_range_t) { .base = base, .len = len };
    *out_handle = range;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    heap_free(handle, sizeof(io_range_t));
}

uacpi_status uacpi_kernel_io_read8([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u8* out_value) {
    io_range_t* range = (io_range_t*) io_range;

    if (offset >= range->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    *out_value = (uint8_t) port_read8(range->base + offset);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u16* out_value) {
    io_range_t* range = (io_range_t*) io_range;

    if (offset >= range->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    *out_value = (uint8_t) port_read16(range->base + offset);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u32* out_value) {
    io_range_t* range = (io_range_t*) io_range;

    if (offset >= range->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    *out_value = (uint8_t) port_read32(range->base + offset);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u8 in_value) {
    io_range_t* rng = (io_range_t*) io_range;

    if (offset >= rng->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    port_write8(rng->base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u16 in_value) {
    io_range_t* rng = (io_range_t*) io_range;

    if (offset >= rng->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    port_write16(rng->base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u32 in_value) {
    io_range_t* rng = (io_range_t*) io_range;

    if (offset >= rng->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    port_write32(rng->base + offset, in_value);
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_alloc([[maybe_unused]] uacpi_size size) {
    return heap_alloc(size);
}

void* uacpi_kernel_calloc([[maybe_unused]] uacpi_size count) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return nullptr;
}

#ifndef UACPI_SIZED_FREES
#error UACPI_SIZED_FREES expected
#else
void uacpi_kernel_free(void* mem, uacpi_size size_hint) {
    heap_free(mem, size_hint);
}
#endif

uacpi_u64 uacpi_kernel_get_ticks(void) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return 0;
}

void uacpi_kernel_stall([[maybe_unused]] uacpi_u8 usec) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

void uacpi_kernel_sleep([[maybe_unused]] uacpi_u64 msec) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

uacpi_handle uacpi_kernel_create_mutex() {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return 0;
}

void uacpi_kernel_free_mutex([[maybe_unused]] uacpi_handle f) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

void uacpi_kernel_free_spinlock([[maybe_unused]] uacpi_handle f) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

uacpi_handle uacpi_kernel_create_event() {
    size_t* counter = heap_alloc(sizeof(size_t));
    *counter = 0;
    return counter;
}

void uacpi_kernel_free_event([[maybe_unused]] uacpi_handle foo) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

uacpi_thread_id uacpi_kernel_get_thread_id() {
    // logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    // Thread*  t = sched_get_current_thread();

    // return (void*) ( t == nullptr ? 0 : t->tid);
    return 0;
}

uacpi_status uacpi_kernel_acquire_mutex([[maybe_unused]] uacpi_handle thing, [[maybe_unused]] uacpi_u16 w) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

void uacpi_kernel_release_mutex([[maybe_unused]] uacpi_handle thing) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

uacpi_bool uacpi_kernel_wait_for_event([[maybe_unused]] uacpi_handle foo, [[maybe_unused]] uacpi_u16 bar) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return true;
}

void uacpi_kernel_signal_event([[maybe_unused]] uacpi_handle foo) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

void uacpi_kernel_reset_event([[maybe_unused]] uacpi_handle foo) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

uacpi_status uacpi_kernel_handle_firmware_request([[maybe_unused]] uacpi_firmware_request* foo) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_install_interrupt_handler([[maybe_unused]] uacpi_u32 irq, [[maybe_unused]] uacpi_interrupt_handler handler, [[maybe_unused]] uacpi_handle ctx, [[maybe_unused]] uacpi_handle* out_irq_handle) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);

    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler([[maybe_unused]] uacpi_interrupt_handler foo, [[maybe_unused]] uacpi_handle irq_handle) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_handle uacpi_kernel_create_spinlock() {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);

    return 0;
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock([[maybe_unused]] uacpi_handle lock) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);

    return 1;
}

void uacpi_kernel_unlock_spinlock([[maybe_unused]] uacpi_handle lock, [[maybe_unused]] uacpi_cpu_flags foo) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

uacpi_status uacpi_kernel_schedule_work([[maybe_unused]] uacpi_work_type t, [[maybe_unused]] uacpi_work_handler f, [[maybe_unused]] uacpi_handle ctx) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    f(ctx);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion() {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot() {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return 0;
}
