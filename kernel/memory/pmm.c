#include "memory/pmm.h"

#include <stddef.h>

#include "common/log.h"
#include "common/lock/spinlock.h"
#include "lib/mem.h"
#include "memory/hhdm.h"

#define PAGE_FRAME_SIZE 4096



Spinlock pmm_lock = SPINLOCK_INIT;

static paddr_t free_list_head;

paddr_t pmm_alloc(uint64_t flags) {
    spinlock_acquire(&pmm_lock);

    paddr_t pf = free_list_head;
    free_list_head = *((paddr_t*) HHDM(pf));

    if ((flags & PMM_ZERO) != 0)
        memclear((void*) pf, PAGE_FRAME_SIZE);

    spinlock_release(&pmm_lock);

    return pf;
}

void pmm_free(paddr_t pf) {
    spinlock_acquire(&pmm_lock);

    *((paddr_t*) HHDM(pf)) = free_list_head;
    free_list_head = pf;

    spinlock_release(&pmm_lock);
}

void pmm_init(Memmap* memmap) {
    uint64_t bytes_of_mem = 0;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        MemmapEntry entry = memmap->entries[i];

        if (!(entry.type == MEMMAP_USABLE)) // TODO: || entry.type == MEMMAP_RECLAIMABLE
            continue;

        bytes_of_mem += entry.length;

        for (uintptr_t pf = entry.base; pf < entry.base + entry.length; pf += PAGE_FRAME_SIZE)
            pmm_free(pf);
    }

    logln(LOG_INFO, "PMM", "Initialized, %u MiB", bytes_of_mem / (1024 * 1024));
}
