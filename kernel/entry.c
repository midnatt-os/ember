#include "sys/boot.h"

#include <stddef.h>

#include "common/assert.h"

#include <limine.h>

#define LIMINE_REQUEST(request, tag, rev) \
[[gnu::used, gnu::section(".limine_requests")]] static volatile struct limine_##request request = { \
.id = tag, \
.revision = rev, \
.response = nullptr \
};

[[gnu::used, gnu::section(".limine_requests")]]
LIMINE_BASE_REVISION(3)
[[gnu::used, gnu::section(".limine_requests")]]
LIMINE_REQUESTS_START_MARKER

LIMINE_REQUEST(framebuffer_request, LIMINE_FRAMEBUFFER_REQUEST, 3)
LIMINE_REQUEST(hhdm_request, LIMINE_HHDM_REQUEST, 3)
LIMINE_REQUEST(memmap_request, LIMINE_MEMMAP_REQUEST, 3)
LIMINE_REQUEST(module_request, LIMINE_MODULE_REQUEST, 3)
LIMINE_REQUEST(kernel_address_request, LIMINE_KERNEL_ADDRESS_REQUEST, 3)
LIMINE_REQUEST(rsdp_request, LIMINE_RSDP_REQUEST, 3)

// SMP
[[gnu::used, gnu::section(".limine_requests")]] static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 3,
    .response = nullptr,
    .flags = 0 // Enable X2APIC if possible.
};

[[gnu::used, gnu::section(".limine_requests")]]
LIMINE_REQUESTS_END_MARKER



[[noreturn]] void bsp_init(BootInfo* boot_info);

BootInfo info;

[[noreturn]] void kentry() {
    ASSERT(framebuffer_request.response != nullptr);
    ASSERT(hhdm_request.response != nullptr);
    ASSERT(memmap_request.response != nullptr);
    ASSERT(module_request.response != nullptr);
    ASSERT(kernel_address_request.response != nullptr);
    ASSERT(rsdp_request.response != nullptr);
    ASSERT(smp_request.response != nullptr);

    // Framebuffer
    info.fb = framebuffer_request.response->framebuffers[0];

    // HHDM
    info.hhdm_offset = hhdm_request.response->offset;

    // Memmap
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];

        MemmapEntryType type;
        switch (entry->type) {
            case LIMINE_MEMMAP_USABLE: type = MEMMAP_USABLE; break;
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: type = MEMMAP_RECLAIMABLE; break;
            default: type = MEMMAP_OTHER; break;
        }

        info.memmap.entry_count++;

        info.memmap.entries[i] = (MemmapEntry) {
            .base = entry->base,
            .length = entry->length,
            .type = type
        };
    }

    // Modules
    for (size_t i = 0; i < module_request.response->module_count; i++) {
        struct limine_file* file = module_request.response->modules[i];

        info.modules.module_count++;

        info.modules.modules[i] = (Module) {
            .address = file->address,
            .size = file->size,
            .path = file->path,       // TODO: maybe strcpy?
            .cmdline = file->cmdline, // TODO: maybe strcpy?
        };
    }

    // Kernel address
    info.kernel_addr = (KernelAddress) {
        .virt_base = kernel_address_request.response->virtual_base,
        .phys_base =  kernel_address_request.response->physical_base
    };

    info.rsdp_address = (uintptr_t) rsdp_request.response->address;

    info.smp = smp_request.response;

    bsp_init(&info);
}
