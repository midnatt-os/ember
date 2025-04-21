#include "gdt.h"

#include "stdint.h"

#define GDT_ENTRY_COUNT 7



typedef struct [[gnu::packed]] {
    uint16_t limit;
    uint64_t address;
} GDTR;

static uint64_t gdt_entries[GDT_ENTRY_COUNT] = {
    0x0,               // 0x0  - null
    0x209b0000000000,  // 0x8  - kernel code
    0x20930000000000,  // 0x10 - kernel data
    0x20fb00,          // 0x18 - user code
    0x20f300,          // 0x20 - user data
    0x0, 0x0           //        TSS
};

static GDTR gdtr = {
    .limit = GDT_ENTRY_COUNT * sizeof(uint64_t) - 1,
    .address = (uint64_t) gdt_entries
};

void gdt_load(GDTR* gdtr, uint64_t selector_code, uint64_t selector_data);

void gdt_init() {
    gdt_load(&gdtr, GDT_SELECTOR_CODE64_RING0, GDT_SELECTOR_DATA64_RING0);
}
