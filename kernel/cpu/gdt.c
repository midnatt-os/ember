#include "cpu/gdt.h"

#include <stdint.h>

#define GDT_ENTRY_COUNT 7

#define ACCESS_PRESENT (1 << 7)
#define ACCESS_DPL(DPL) (((DPL) & 0b11) << 5)
#define ACCESS_TYPE_TSS (9)
#define ACCESS_TYPE_CODE(CONFORM, READ) ((1 << 4) | (1 << 3) | ((CONFORM) << 2) | ((READ) << 1))
#define ACCESS_TYPE_DATA(DIRECTION, WRITE) ((1 << 4) | ((DIRECTION) << 2) | ((WRITE) << 1))
#define ACCESS_ACCESSED (1 << 0)

#define FLAG_GRANULARITY (1 << 7)
#define FLAG_DB (1 << 6)
#define FLAG_LONG (1 << 5)
#define FLAG_SYSTEM_AVL (1 << 4)

typedef struct [[gnu::packed]] {
    uint16_t limit;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t flags;
    uint8_t base_high;
} GdtEntry;

typedef struct [[gnu::packed]] {
    GdtEntry entry;
    uint32_t base_ext;
    uint8_t rsv0;
    uint8_t zero_rsv1;
    uint16_t rsv2;
} GdtSystemEntry;

typedef struct [[gnu::packed]] {
    uint16_t limit;
    uint64_t address;
} GDTR;

static GdtEntry gdt_entries[] = {
    {},
    {
        .limit = 0,
        .base_low = 0,
        .base_mid = 0,
        .access = ACCESS_PRESENT | ACCESS_DPL(0) | ACCESS_TYPE_CODE(0, 1),
        .flags = FLAG_LONG,
        .base_high = 0
    },
    {
        .limit = 0,
        .base_low = 0,
        .base_mid = 0,
        .access = ACCESS_PRESENT | ACCESS_DPL(0) | ACCESS_TYPE_DATA(0, 1),
        .flags = 0,
        .base_high = 0
    },
    {
        .limit = 0,
        .base_low = 0,
        .base_mid = 0,
        .access = ACCESS_PRESENT | ACCESS_DPL(3) | ACCESS_TYPE_DATA(0, 1),
        .flags = 0,
        .base_high = 0
    },
    {
        .limit = 0,
        .base_low = 0,
        .base_mid = 0,
        .access = ACCESS_PRESENT | ACCESS_DPL(3) | ACCESS_TYPE_CODE(0, 1),
        .flags = FLAG_LONG,
        .base_high = 0
    },
    {}, {} // TSS
};

static GDTR gdtr = {
    .limit = sizeof(gdt_entries) - 1,
    .address = (uint64_t) gdt_entries
};

void gdt_load(GDTR* gdtr, uint64_t selector_code, uint64_t selector_data);

void gdt_init() {
    gdt_load(&gdtr, GDT_SELECTOR_CODE64_RING0, GDT_SELECTOR_DATA64_RING0);
}

void gdt_load_tss(Tss* tss) {
    uint16_t tss_segment = sizeof(gdt_entries) - 16;

    GdtSystemEntry* entry = (GdtSystemEntry*) ((uintptr_t) gdt_entries + tss_segment);
    entry->entry.access = ACCESS_PRESENT | ACCESS_TYPE_TSS;
    entry->entry.flags = FLAG_SYSTEM_AVL | ((sizeof(Tss) >> 16) & 0b1111);
    entry->entry.limit = (uint16_t) sizeof(Tss);
    entry->entry.base_low = (uint16_t) (uint64_t) tss;
    entry->entry.base_mid = (uint8_t) ((uint64_t) tss >> 16);
    entry->entry.base_high = (uint8_t) ((uint64_t) tss >> 24);
    entry->base_ext = (uint32_t) ((uint64_t) tss >> 32);

    asm volatile("ltr %0" : : "m"(tss_segment));
}
