#include "common/limine_requests.h"

#include "lib/string.h"
#include "limine.h"

#include <stddef.h>

#define LIMINE_REQUEST(request, tag, rev) [[gnu::used, gnu::section(".limine_requests")]] volatile struct limine_##request request = { .id = tag, .revision = rev, .response = nullptr }

[[gnu::used, gnu::section(".limine_requests")]] LIMINE_BASE_REVISION(4);
[[gnu::used, gnu::section(".limine_requests")]] LIMINE_REQUESTS_START_MARKER;

LIMINE_REQUEST(framebuffer_request, LIMINE_FRAMEBUFFER_REQUEST, 4);
LIMINE_REQUEST(hhdm_request, LIMINE_HHDM_REQUEST, 4);
LIMINE_REQUEST(memmap_request, LIMINE_MEMMAP_REQUEST, 4);
LIMINE_REQUEST(module_request, LIMINE_MODULE_REQUEST, 4);
LIMINE_REQUEST(executable_address_request, LIMINE_EXECUTABLE_ADDRESS_REQUEST, 4);
LIMINE_REQUEST(rsdp_request, LIMINE_RSDP_REQUEST, 4);
LIMINE_REQUEST(mp_request, LIMINE_MP_REQUEST, 4);
LIMINE_REQUEST(executable_file_request, LIMINE_EXECUTABLE_FILE_REQUEST, 4);

[[gnu::used, gnu::section(".limine_requests")]] LIMINE_REQUESTS_END_MARKER;

struct limine_file* find_limine_module(const char* name) {
    for (size_t i = 0; i < module_request.response->module_count; i++) {
        if (streq(module_request.response->modules[i]->string, name))
            return module_request.response->modules[i];
    }

    return nullptr;
}
