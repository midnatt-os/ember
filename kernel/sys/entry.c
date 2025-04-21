#include "sys/boot.h"

#include <limine.h>

#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "sys/stack_trace.h"

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

LIMINE_REQUEST(module_request, LIMINE_MODULE_REQUEST, 3)

[[gnu::used, gnu::section(".limine_requests")]]
LIMINE_REQUESTS_END_MARKER



BootInfo info;

void kentry() {
    //if (!memmap_request.response)

    // Modules
    for (uint64_t i = 0; i < module_request.response->module_count; i++) {
        struct limine_file* file = module_request.response->modules[i];

        info.modules[i] = (Module) {
            .address = file->address,
            .size = file->size,
            .path = file->path,       // TODO: maybe strcpy?
            .cmdline = file->cmdline, // TODO: maybe strcpy?
        };
    }

    load_kernel_symbols(info.modules, MODULE_COUNT); // TODO: move to kinit?

    while (true)
        cpu_halt();
}
