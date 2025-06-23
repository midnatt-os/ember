#include "cpu/ioapic.h"

#include "common/assert.h"
#include "common/log.h"
#include "memory/heap.h"

#include "memory/vm.h"
#include "uacpi/acpi.h"
#include "uacpi/tables.h"

#define IOAPIC_REG_IOREGSEL  0x00
#define IOAPIC_REG_IOWIN     0x10

#define IOAPIC_REGID 0x00
#define IOAPIC_VER   0x01
#define IOAPIC_ARB   0x02

#define IOAPIC_REDIR_TABLE_BASE 0x10

#define LEGACY_POLARITY (0b11)
#define LEGACY_TRIGGER (0b11 << 2)
#define LEGACY_POLARITY_HIGH 0b1
#define LEGACY_POLARITY_LOW 0b11
#define LEGACY_TRIGGER_EDGE (0b1 << 2)
#define LEGACY_TRIGGER_LEVEL (0b11 << 2)

typedef struct {
    uint8_t gsi;
    uint16_t flags;
} IsoRecord;



static IsoRecord legacy_irq_map[16] = {
    { 0,  0 },
    { 1,  0 },
    { 2,  0 },
    { 3,  0 },
    { 4,  0 },
    { 5,  0 },
    { 6,  0 },
    { 7,  0 },
    { 8,  0 },
    { 9,  0 },
    { 10, 0 },
    { 11, 0 },
    { 12, 0 },
    { 13, 0 },
    { 14, 0 },
    { 15, 0 }
};

uintptr_t ioapic = 0;

static inline void ioapic_write(uint8_t reg, uint32_t value) {
    *(volatile uint32_t*)(ioapic + IOAPIC_REG_IOREGSEL) = reg;
    *(volatile uint32_t*)(ioapic + IOAPIC_REG_IOWIN) = value;
}

[[maybe_unused]] static inline uint32_t ioapic_read(uint8_t reg) {
    *(volatile uint32_t*)(ioapic + IOAPIC_REG_IOREGSEL) = reg;
    return *(volatile uint32_t*)(ioapic + IOAPIC_REG_IOWIN);
}

void ioapic_redirect_irq(uint8_t irq, uint8_t vector, uint8_t lapic_id, bool enabled) {
    IsoRecord iso = legacy_irq_map[irq];
    logln(LOG_DEBUG, "IOAPIC", "Redirecting IRQ%d (GSI %d) → vector 0x%02x, flags=0x%02x", irq, iso.gsi, vector, iso.flags);
    uint8_t index = iso.gsi;

    uint32_t low = 0;
    low |= vector;              // Interrupt vector
    low |= (0 << 8);            // Delivery Mode: Fixed
    low |= (0 << 11);           // Destination Mode: Physical

    switch (iso.flags & LEGACY_POLARITY) {
        case LEGACY_POLARITY_HIGH: low |= (0 << 13); break;
        case LEGACY_POLARITY_LOW: low |= (1 << 13); break;
    }

    switch (iso.flags & LEGACY_TRIGGER) {
        case LEGACY_TRIGGER_EDGE: low |= (0 << 15); break;
        case LEGACY_TRIGGER_LEVEL: low |= (1 << 15); break;
    }

    if (!enabled)
        low |= (1 << 16); // Masked

    uint32_t high = lapic_id << 24;

    ioapic_write(IOAPIC_REDIR_TABLE_BASE + 2 * index + 1, high);
    ioapic_write(IOAPIC_REDIR_TABLE_BASE + 2 * index, low);
}




void ioapic_init() {
    uintptr_t ioapic_addr = 0;

    uacpi_table madt_table;
    ASSERT(uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &madt_table) == UACPI_STATUS_OK);
    struct acpi_madt* madt = madt_table.ptr;

    size_t offset = 0;
    while (offset < madt->hdr.length - sizeof(*madt)) {
        struct acpi_entry_hdr* entry = (struct acpi_entry_hdr*) (((uintptr_t) madt->entries) + offset);

        switch (entry->type) {
            case ACPI_MADT_ENTRY_TYPE_IOAPIC: ioapic_addr = ((struct acpi_madt_ioapic*) entry)->address; break;
            case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE:
                struct acpi_madt_interrupt_source_override* override = (struct acpi_madt_interrupt_source_override*) entry;
                legacy_irq_map[override->source] = (IsoRecord) { .gsi = override->gsi, .flags = override->flags };
                break;

            default: break;
        }

        offset += entry->length;
    }

    ioapic = (uintptr_t) vm_map_direct(&kernel_as, nullptr, PAGE_SIZE, ioapic_addr, VM_PROT_RW, VM_CACHING_UNCACHED, VM_FLAG_NONE);
    ASSERT(ioapic != 0);

    uacpi_table_unref(&madt_table);
    logln(LOG_INFO, "IOAPIC", "Initialized");
}
