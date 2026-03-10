#include "mem/pmm.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/limine_requests.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "lib/mem.h"
#include "mem/hhdm.h"
#include "mem/page.h"
#include "sys/init.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define PMM_POISON_WORD 0

static spinlock_t pmm_lock = SPINLOCK_NEW;

static uintptr_t freelist = 0;

uint64_t pf_total_count = 0;
uint64_t pf_use_count = 0;
static uint64_t pmm_epoch = 0;

static size_t pmm_early_entry_index = 0;
static uintptr_t pmm_early_cursor = 0;
static uintptr_t pmm_early_entry_end = 0;

static void pmm_poison_free_page(uintptr_t phys, uintptr_t next) {
    uintptr_t* words = (uintptr_t*) HHDM(phys);
    size_t words_count = PAGE_SIZE / sizeof(uintptr_t);

    words[0] = next;
    for (size_t i = 1; i < words_count; i++)
        words[i] = PMM_POISON_WORD;
}

static void pmm_check_free_page(uintptr_t phys) {
    ASSERT((phys % PAGE_SIZE) == 0);
    size_t pfn = phys_to_pfn(phys);
    ASSERT(pfn < page_db_pages);

    uintptr_t* words = (uintptr_t*) HHDM(phys);
    size_t words_count = PAGE_SIZE / sizeof(uintptr_t);

    uintptr_t next = words[0];
    if (next != 0) {
        if ((next % PAGE_SIZE) != 0 || phys_to_pfn(next) >= page_db_pages) {
            page_t* pg = &page_db[pfn];
            log_raw("PMM: free page dump @%#lx: w0=%#lx w1=%#lx w2=%#lx w3=%#lx\n", (unsigned long) phys, (unsigned long) words[0], (unsigned long) words[1], (unsigned long) words[2], (unsigned long) words[3]);
            panic(
                "PMM: corrupted freelist pointer %#lx in page %#lx (last alloc %#lx @%llu, last free %#lx @%llu)",
                (unsigned long) next,
                (unsigned long) phys,
                (unsigned long) pg->last_alloc_caller,
                (unsigned long long) pg->last_alloc_epoch,
                (unsigned long) pg->last_free_caller,
                (unsigned long long) pg->last_free_epoch
            );
        }
    }

    for (size_t i = 1; i < words_count; i++) {
        if (words[i] != PMM_POISON_WORD) {
            page_t* pg = &page_db[pfn];
            log_raw("PMM: free page dump @%#lx: w0=%#lx w1=%#lx w2=%#lx w3=%#lx\n", (unsigned long) phys, (unsigned long) words[0], (unsigned long) words[1], (unsigned long) words[2], (unsigned long) words[3]);
            panic(
                "PMM: free page %#lx corrupted at word %zu (value=%#lx, last alloc %#lx @%llu, last free %#lx @%llu)",
                (unsigned long) phys,
                i,
                (unsigned long) words[i],
                (unsigned long) pg->last_alloc_caller,
                (unsigned long long) pg->last_alloc_epoch,
                (unsigned long) pg->last_free_caller,
                (unsigned long long) pg->last_free_epoch
            );
        }
    }
}

static bool pmm_early_next_region(void) {
    struct limine_memmap_response* memmap = memmap_request.response;
    ASSERT(memmap != nullptr);

    while (pmm_early_entry_index < memmap->entry_count) {
        struct limine_memmap_entry* entry = memmap->entries[pmm_early_entry_index++];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        uintptr_t start = ALIGN_UP(entry->base, PAGE_SIZE);
        uintptr_t end = ALIGN_DOWN(entry->base + entry->length, PAGE_SIZE);
        if (start >= end)
            continue;

        pmm_early_cursor = start;
        pmm_early_entry_end = end;
        return true;
    }

    return false;
}

uintptr_t pmm_early_alloc(size_t pages) {
    ASSERT(pages != 0);

    size_t bytes = pages * PAGE_SIZE;

    while (true) {
        if (pmm_early_cursor >= pmm_early_entry_end) {
            if (!pmm_early_next_region())
                panic("pmm_early_alloc exhausted all usable memory\n");
        }

        uintptr_t start = ALIGN_UP(pmm_early_cursor, PAGE_SIZE);
        if (start + bytes <= pmm_early_entry_end) {
            pmm_early_cursor = start + bytes;
            return start;
        }

        pmm_early_cursor = pmm_early_entry_end;
    }
}

uintptr_t pmm_alloc(uint64_t flags) {
    bool prev = spinlock_lock(&pmm_lock);

    uintptr_t pf = freelist;
    ASSERT(pf != 0);
    ASSERT((pf % PAGE_SIZE) == 0);
    ASSERT(phys_to_pfn(pf) < page_db_pages);

    pmm_check_free_page(pf);
    freelist = *((uintptr_t*) HHDM(pf));
    page_t* pg = phys_to_page(pf);
    ASSERT(pg->flags & PAGE_FLAG_PRESENT);
    ASSERT(pg->refcount == 0);
    pg->refcount = 1;
    pg->last_alloc_caller = (uintptr_t) __builtin_return_address(0);
    pg->last_alloc_epoch = ++pmm_epoch;

    pf_use_count++;

    spinlock_unlock(&pmm_lock, prev);

    if ((flags & PMM_ZERO) != 0)
        memclear((void*) HHDM(pf), PAGE_SIZE);

    return pf;
}

void pmm_free(uintptr_t ptr) {
    ASSERT(ptr != 0xaaaa2aaaaaaaaaaa);
    ASSERT(ptr != 0xaaaaaaaaaaaaaaaa);
    bool prev = spinlock_lock(&pmm_lock);

    ASSERT((ptr % PAGE_SIZE) == 0);
    ASSERT(phys_to_pfn(ptr) < page_db_pages);
    page_t* pg = phys_to_page(ptr);
    ASSERT(pg->flags & PAGE_FLAG_PRESENT);
    ASSERT(pg->refcount == 0);
    pg->last_free_caller = (uintptr_t) __builtin_return_address(0);
    pg->last_free_epoch = ++pmm_epoch;

    pmm_poison_free_page(ptr, freelist);
    freelist = ptr;

    pf_use_count--;

    spinlock_unlock(&pmm_lock, prev);
}

INIT_TARGET(pmm, INIT_STAGE_EARLY, INIT_SCOPE_BSP, INIT_DEPS()) {
    struct limine_memmap_response* memmap = memmap_request.response;
    page_db_early_discover(memmap);
    page_db_early_alloc();
    page_db_init_flags(memmap);

    freelist = 0;
    pf_total_count = 0;

    for (size_t pfn = 0; pfn < page_db_pages; pfn++) {
        page_t* page = &page_db[pfn];
        if ((page->flags & (PAGE_FLAG_PRESENT | PAGE_FLAG_USABLE)) != (PAGE_FLAG_PRESENT | PAGE_FLAG_USABLE))
            continue;
        if ((page->flags & PAGE_FLAG_RESERVED) != 0)
            continue;

        uintptr_t phys = pfn_to_phys(pfn);
        pmm_poison_free_page(phys, freelist);
        freelist = phys;
        pf_total_count++;
    }

    pf_use_count = 0;

    uint64_t page_db_bytes = (uint64_t) page_db_phys_page_count * PAGE_SIZE;
    logln(LOG_DEBUG, "PMM", "Page database reserved %lu MiB (%lu pages)", page_db_bytes / (1024 * 1024), page_db_phys_page_count);
    logln(LOG_DEBUG, "PMM", "Initialized with %lu MiB", pf_total_count / 256);
}
