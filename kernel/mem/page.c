#include "mem/page.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/limine_requests.h"
#include "lib/mem.h"
#include "mem/hhdm.h"
#include "mem/pmm.h"

#include <stdbool.h>

page_t* page_db = nullptr;
size_t page_db_pages = 0;
uint64_t page_db_max_phys = 0;
uintptr_t page_db_phys = 0;
size_t page_db_phys_page_count = 0;

static bool memtype_is_dram(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        case LIMINE_MEMMAP_ACPI_NVS:
#if LIMINE_API_REVISION >= 2
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
#else
        case LIMINE_MEMMAP_KERNEL_AND_MODULES:
#endif
            return true;
        default: return false;
    }
}

static void page_db_mark_reserved(uint64_t start, uint64_t end) {
    if (start >= end)
        return;

    start = ALIGN_DOWN(start, PAGE_SIZE);
    end = ALIGN_UP(end, PAGE_SIZE);

    for (uint64_t phys = start; phys < end; phys += PAGE_SIZE) {
        size_t pfn = phys_to_pfn(phys);
        if (pfn >= page_db_pages)
            continue;

        page_t* pg = &page_db[pfn];
        pg->flags |= PAGE_FLAG_PRESENT | PAGE_FLAG_RESERVED;
        pg->refcount = 0;
        pg->flags &= ~PAGE_FLAG_USABLE;
    }
}

void page_db_early_discover(struct limine_memmap_response* memmap) {
    ASSERT(memmap != nullptr);

    uint64_t max_phys = 0;
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (!memtype_is_dram(entry->type))
            continue;

        uint64_t end = entry->base + entry->length;
        if (end > max_phys)
            max_phys = end;
    }

    if (max_phys == 0)
        return;

    page_db_max_phys = ALIGN_UP(max_phys, PAGE_SIZE);
    page_db_pages = (size_t) (page_db_max_phys >> PAGE_SHIFT);
}

void page_db_early_alloc(void) {
    if (page_db_pages == 0)
        return;

    size_t bytes = page_db_pages * sizeof(page_t);
    size_t pages = DIV_CEIL(bytes, PAGE_SIZE);

    uintptr_t phys = pmm_early_alloc(pages);
    page_db_phys = phys;
    page_db_phys_page_count = pages;

    page_db = (page_t*) HHDM(phys);
    memset(page_db, 0, bytes);
}

void page_db_init_flags(struct limine_memmap_response* memmap) {
    ASSERT(memmap != nullptr);
    if (page_db == nullptr || page_db_pages == 0)
        return;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (!memtype_is_dram(entry->type))
            continue;

        uint64_t start = ALIGN_DOWN(entry->base, PAGE_SIZE);
        uint64_t end = ALIGN_UP(entry->base + entry->length, PAGE_SIZE);
        for (uint64_t phys = start; phys < end; phys += PAGE_SIZE) {
            size_t pfn = phys_to_pfn(phys);
            if (pfn >= page_db_pages)
                continue;

            page_t* pg = &page_db[pfn];
            pg->flags |= PAGE_FLAG_PRESENT;

            if (entry->type == LIMINE_MEMMAP_USABLE) {
                pg->flags |= PAGE_FLAG_USABLE;
            } else {
                pg->flags |= PAGE_FLAG_RESERVED;
                pg->flags &= ~PAGE_FLAG_USABLE;
            }
        }
    }

    extern uint8_t __TEXT_START[];
    extern uint8_t __BSS_END[];

    struct limine_executable_address_response* kernel_addr = executable_address_request.response;
    ASSERT(kernel_addr != nullptr);

    uint64_t virt_start = (uint64_t) __TEXT_START;
    uint64_t virt_end = (uint64_t) __BSS_END;
    uint64_t kernel_phys_start = kernel_addr->physical_base + (virt_start - kernel_addr->virtual_base);
    uint64_t kernel_phys_end = kernel_addr->physical_base + (virt_end - kernel_addr->virtual_base);

    page_db_mark_reserved(kernel_phys_start, kernel_phys_end);

    if (page_db_phys != 0 && page_db_phys_page_count != 0) {
        page_db_mark_reserved(page_db_phys, page_db_phys + page_db_phys_page_count * PAGE_SIZE);
    }
}

static page_t* page_from_phys(uintptr_t phys) {
    ASSERT(page_db != nullptr);
    size_t pfn = phys_to_pfn(phys);
    ASSERT(pfn < page_db_pages);
    return &page_db[pfn];
}

void page_ref_inc(uintptr_t phys) {
    page_t* pg = page_from_phys(phys);
    pg->refcount++;
}

uint32_t page_ref_dec(uintptr_t phys) {
    page_t* pg = page_from_phys(phys);
    ASSERT(pg->refcount > 0);
    return --pg->refcount;
}

uint32_t page_ref_get(uintptr_t phys) {
    page_t* pg = page_from_phys(phys);
    return pg->refcount;
}
