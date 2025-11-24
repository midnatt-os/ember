#include "mem/pmm.h"

#include "common/assert.h"
#include "common/limine_requests.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "lib/mem.h"
#include "mem/hhdm.h"
#include "mem/page.h"

#include <stddef.h>
#include <stdint.h>


static spinlock_t pmm_lock = SPINLOCK_NEW;

static uintptr_t freelist = 0;

uint64_t pf_total_count = 0;
uint64_t pf_use_count = 0;

uintptr_t pmm_alloc(uint64_t flags) {
    bool prev = spinlock_lock(&pmm_lock);

    uintptr_t pf = freelist;

    ASSERT(pf != 0);

    freelist = *((uintptr_t*) HHDM(pf));

    pf_use_count++;

    spinlock_unlock(&pmm_lock, prev);

    if ((flags & PMM_ZERO) != 0)
        memclear((void*) HHDM(pf), PAGE_SIZE);

    return pf;
}

void pmm_free(uintptr_t ptr) {
    bool prev = spinlock_lock(&pmm_lock);

    *((uintptr_t*) HHDM(ptr)) = freelist;
    freelist = ptr;

    pf_use_count--;

    spinlock_unlock(&pmm_lock, prev);
}

void pmm_init() {
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            for (size_t addr = entry->base; addr < entry->base + entry->length; addr += PAGE_SIZE) {
                pf_total_count++;
                pmm_free(addr);
            }
        }
    }

    pf_use_count = 0;

    logln(LOG_INFO, "PMM", "Initialized with %lu MiB", pf_total_count / 256);
}
