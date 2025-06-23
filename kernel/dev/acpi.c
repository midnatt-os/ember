#include <stdint.h>
#include <uacpi/uacpi.h>

#include "common/assert.h"
#include "common/lock/mutex.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "common/util.h"
#include "cpu/interrupts.h"
#include "cpu/io.h"
#include "cpu/ioapic.h"
#include "dev/pci.h"
#include "lib/list.h"
#include "memory/heap.h"
#include "memory/hhdm.h"

#include "memory/vm.h"
#include "sched/sched.h"
#include "sys/time.h"
#include "uacpi/status.h"
#include "uacpi/event.h"
#include "uacpi/types.h"
#include "uacpi/uacpi.h"

typedef struct {
    uacpi_io_addr base;
    uacpi_size len;
} IoRange;


uintptr_t _rsdp_address;

void acpi_early_init(uintptr_t rsdp_address) {
    _rsdp_address = rsdp_address;

    void* tmp_buffer = kmalloc(4096);
    uacpi_setup_early_table_access(tmp_buffer, 4096);
}

void acpi_finalize_init() {
    ASSERT(uacpi_initialize(0) == UACPI_STATUS_OK);
    ASSERT(uacpi_namespace_load() == UACPI_STATUS_OK);
    ASSERT(uacpi_namespace_initialize() == UACPI_STATUS_OK);
    ASSERT(uacpi_finalize_gpe_initialization() == UACPI_STATUS_OK);
}

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    *out_rsdp_address = PHYS_FROM_HHDM(_rsdp_address);
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_map(uacpi_phys_addr addr, [[maybe_unused]] uacpi_size len) {
    size_t offset = addr % PAGE_SIZE;
    uintptr_t ret = (uintptr_t) vm_map_direct(&kernel_as, nullptr, ALIGN_UP(len + offset, PAGE_SIZE), ALIGN_DOWN(addr, PAGE_SIZE), VM_PROT_RW, VM_CACHING_UNCACHED, VM_FLAG_NONE);
    return (void *) (ret + offset);
}

void uacpi_kernel_unmap([[maybe_unused]] void* addr, [[maybe_unused]] uacpi_size len) {
    //logln(LOG_WARN, "UACPI", "uacpi_kernel_unmap() is stubbed");
}


UACPI_PRINTF_DECL(2, 3)
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *fmt, ...) {
    va_list list;
    va_start(list, fmt);

    switch (level) {
        case UACPI_LOG_TRACE:
        case UACPI_LOG_DEBUG:
        //case UACPI_LOG_INFO: break;
        case UACPI_LOG_INFO: log_list(LOG_INFO, "UACPI", fmt, list); break;
        case UACPI_LOG_WARN: log_list(LOG_WARN, "UACPI", fmt, list); break;
        case UACPI_LOG_ERROR: log_list(LOG_ERROR, "UACPI", fmt, list); break;
    }

    va_end(list);
}

uacpi_status uacpi_kernel_raw_memory_read(uacpi_phys_addr address, uacpi_u8 byte_width, uacpi_u64 *out_value) {
    uint64_t virt = HHDM(address);

    switch (byte_width) {
        case 1: *out_value = *(volatile uint8_t*) virt; break;
        case 2: *out_value = *(volatile uint16_t*) virt; break;
        case 4: *out_value = *(volatile uint32_t*) virt; break;
        case 8: *out_value = *(volatile uint64_t*) virt; break;
        default: return UACPI_STATUS_INVALID_ARGUMENT;
    }

    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_raw_memory_write(uacpi_phys_addr address, uacpi_u8 byte_width, uacpi_u64 in_value) {
    uint64_t virt = HHDM(address);

    switch (byte_width) {
        case 1: *(volatile uint8_t *) virt = in_value; break;
        case 2: *(volatile uint16_t *) virt = in_value; break;
        case 4: *(volatile uint32_t *) virt = in_value; break;
        case 8: *(volatile uint64_t *) virt = in_value; break;
        default: return UACPI_STATUS_INVALID_ARGUMENT;
    }

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_raw_io_read([[maybe_unused]] uacpi_io_addr address, [[maybe_unused]] uacpi_u8 byte_width, [[maybe_unused]] uacpi_u64 *out_value) {
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

uacpi_status uacpi_kernel_pci_read8([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u8 *value) {
    *value = pci_cfg_read_8(device, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u16 *value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read32([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]]  uacpi_u32 *value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write8([[maybe_unused]]  uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u8 value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write16([[maybe_unused]] uacpi_handle device, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u16 value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write32([[maybe_unused]] uacpi_handle device, [[maybe_unused]]  uacpi_size offset, [[maybe_unused]] uacpi_u32 value) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_device_open([[maybe_unused]] uacpi_pci_address address, [[maybe_unused]] uacpi_handle *out_handle) {
    PciDevice* device = kmalloc(sizeof(PciDevice));

    *device = (PciDevice) {
        .domain = address.segment,
        .bus = address.bus,
        .dev = address.device,
        .fn = address.function
    };

    spinlock_acquire(&pci_devices_lock);
    list_append(&pci_devices, &device->list_node);
    spinlock_release(&pci_devices_lock);

    *out_handle = device;

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle) {
    IoRange* range = kmalloc(sizeof(IoRange));
    *range = (IoRange) { .base = base, .len = len };
    *out_handle = range;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    kfree(handle);
}

uacpi_status uacpi_kernel_io_read8([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u8 *out_value) {
    IoRange* range = (IoRange*) io_range;

    if (offset >= range->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    *out_value = (uint8_t) inb(range->base + offset);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u16 *out_value) {
    IoRange* range = (IoRange*) io_range;

    if (offset >= range->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    *out_value = (uint8_t) inw(range->base + offset);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u32 *out_value) {
    IoRange* range = (IoRange*) io_range;

    if (offset >= range->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    *out_value = (uint8_t) ind(range->base + offset);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u8 in_value) {
    IoRange* rng = (IoRange*) io_range;

    if (offset >= rng->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    outb(rng->base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u16 in_value) {
    IoRange* rng = (IoRange*) io_range;

    if (offset >= rng->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    outw(rng->base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32([[maybe_unused]] uacpi_handle io_range, [[maybe_unused]] uacpi_size offset, [[maybe_unused]] uacpi_u32 in_value) {
    IoRange* rng = (IoRange*) io_range;

    if (offset >= rng->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    outd(rng->base + offset, in_value);
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_alloc([[maybe_unused]] uacpi_size size) {
    return kmalloc(size);
}

void* uacpi_kernel_calloc([[maybe_unused]] uacpi_size count) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return nullptr;
}

void uacpi_kernel_free(void *mem) {
    kfree(mem);
}

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
    Mutex* m = kmalloc(sizeof(Mutex));
    *m = MUTEX_NEW;
    return m;
}

void uacpi_kernel_free_mutex(uacpi_handle f) {
    kfree(f);
}

void uacpi_kernel_free_spinlock([[maybe_unused]] uacpi_handle f) {
    kfree(f);
}

uacpi_handle uacpi_kernel_create_event() {
    size_t* counter = kmalloc(sizeof(size_t));
    *counter = 0;
    return counter;
}

void uacpi_kernel_free_event([[maybe_unused]] uacpi_handle foo) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
}

uacpi_thread_id uacpi_kernel_get_thread_id() {
    //logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    //Thread*  t = sched_get_current_thread();

    //return (void*) ( t == nullptr ? 0 : t->tid);
    return 0;
}

uacpi_status uacpi_kernel_acquire_mutex([[maybe_unused]] uacpi_handle thing, [[maybe_unused]] uacpi_u16 w) {
    mutex_acquire((Mutex*) thing);
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex([[maybe_unused]] uacpi_handle thing) {
    mutex_release((Mutex*) thing);
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

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle *out_irq_handle) {
    InterruptEntry* entry = kmalloc(sizeof(InterruptEntry));
    *entry = (InterruptEntry) {
        .type = INT_HANDLER_UACPI,
        .uacpi_handler = handler,
        .arg = ctx
    };

    uint8_t vec = interrupts_request_vector(entry);
    ASSERT(vec >= 0);

    ioapic_redirect_irq(irq, vec, 0, true);
    *out_irq_handle = entry;

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler([[maybe_unused]] uacpi_interrupt_handler foo, [[maybe_unused]] uacpi_handle irq_handle) {
    logln(LOG_WARN, "UACPI", "%s() is stubbed", __FUNCTION__);
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_handle uacpi_kernel_create_spinlock() {
    Spinlock* s = kmalloc(sizeof(Spinlock));
    *s = SPINLOCK_NEW;
    return s;
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock([[maybe_unused]] uacpi_handle lock) {
    spinlock_acquire((Spinlock*) lock);
    return 1;
}

void uacpi_kernel_unlock_spinlock([[maybe_unused]] uacpi_handle lock, [[maybe_unused]] uacpi_cpu_flags foo) {
    spinlock_release((Spinlock*) lock);
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
    return time_current();
}
