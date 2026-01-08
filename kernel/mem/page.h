#pragma once

#include <stddef.h>
#include <stdint.h>

struct limine_memmap_response;

#define PAGE_SIZE 0x1000UL
#define PAGE_SHIFT 12u

typedef struct page {
    uint32_t flags;
    uint32_t refcount;
    uintptr_t last_alloc_caller;
    uintptr_t last_free_caller;
    uint64_t last_alloc_epoch;
    uint64_t last_free_epoch;
} page_t;

enum {
    PAGE_FLAG_PRESENT = 1u << 0,
    PAGE_FLAG_USABLE = 1u << 1,
    PAGE_FLAG_RESERVED = 1u << 2,
};

extern page_t* page_db;
extern size_t page_db_pages;
extern uint64_t page_db_max_phys;
extern uintptr_t page_db_phys;
extern size_t page_db_phys_page_count;

static inline size_t phys_to_pfn(uint64_t phys) {
    return (size_t) (phys >> PAGE_SHIFT);
}

static inline uint64_t pfn_to_phys(size_t pfn) {
    return (uint64_t) pfn << PAGE_SHIFT;
}

static inline page_t* pfn_to_page(size_t pfn) {
    return &page_db[pfn];
}

static inline page_t* phys_to_page(uint64_t phys) {
    return pfn_to_page(phys_to_pfn(phys));
}

static inline uint64_t page_to_phys(page_t* pg) {
    size_t pfn = (size_t) (pg - page_db);
    return pfn_to_phys(pfn);
}

void page_db_early_discover(struct limine_memmap_response* memmap);
void page_db_early_alloc(void);
void page_db_init_flags(struct limine_memmap_response* memmap);
void page_ref_inc(uintptr_t phys);
uint32_t page_ref_dec(uintptr_t phys);
uint32_t page_ref_get(uintptr_t phys);
