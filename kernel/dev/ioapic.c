#include "dev/ioapic.h"

#include "common/assert.h"
#include "common/log.h"
#include "mem/mmio.h"
#include "mem/page.h"
#include "uacpi/acpi.h"
#include "uacpi/status.h"
#include "uacpi/tables.h"

#include <stdint.h>

#define IOAPICID 0x00
#define IOAPICVER 0x01
#define IOREDTBL(n) (0x10 + 2u * (n))

#define IOAPIC_REDIR_DELIV_FIXED (0ull << 8)
#define IOAPIC_REDIR_DEST_PHYSICAL (0ull << 11)
#define IOAPIC_REDIR_POLARITY_LOW (1ull << 13)
#define IOAPIC_REDIR_TRIGGER_LEVEL (1ull << 15)
#define IOAPIC_REDIR_MASK (1ull << 16)

typedef struct {
    volatile uint32_t* regsel;
    volatile uint32_t* win;
    uint32_t gsi_base;
    uint32_t redir_count;
} ioapic_t;

#define MAX_IOAPICS 8

typedef struct {
    bool present;
    uint32_t gsi;
    uint16_t flags;
} iso_map_t;

static ioapic_t g_ioapics[MAX_IOAPICS];
static size_t g_ioapic_count;

static iso_map_t g_iso[16];


static inline uint32_t ioapic_read(ioapic_t* io, uint8_t reg) {
    *io->regsel = reg;
    return *io->win;
}

static inline void ioapic_write(ioapic_t* io, uint8_t reg, uint32_t val) {
    *io->regsel = reg;
    *io->win = val;
}

static inline void ioapic_write_redir(ioapic_t* io, uint32_t pin, uint64_t entry) {
    ioapic_write(io, (uint8_t) IOREDTBL(pin) + 1, (uint32_t) (entry >> 32));
    ioapic_write(io, (uint8_t) IOREDTBL(pin), (uint32_t) entry);
}

static void ioapic_map(ioapic_t* io, uint32_t phys_addr, uint32_t gsi_base) {
    uintptr_t page = phys_addr & ~(uintptr_t) 0xFFF;
    uintptr_t off = phys_addr & (uintptr_t) 0xFFF;

    void* v = mmio_map(page, 2 * PAGE_SIZE);
    uintptr_t base = (uintptr_t) v + off;

    io->regsel = (volatile uint32_t*) base;
    io->win = (volatile uint32_t*) (base + 0x10);
    io->gsi_base = gsi_base;

    uint32_t ver = ioapic_read(io, IOAPICVER);
    io->redir_count = ((ver >> 16) & 0xFFu) + 1u;
}

static void decode_iso_flags(uint16_t flags, bool* active_low, bool* level) {
    uint16_t pol = flags & ACPI_MADT_POLARITY_MASK;
    uint16_t trg = flags & ACPI_MADT_TRIGGERING_MASK;

    *active_low = false;
    *level = false;

    if (pol == ACPI_MADT_POLARITY_ACTIVE_LOW)
        *active_low = true;

    if (trg == ACPI_MADT_TRIGGERING_LEVEL)
        *level = true;
}

static uint32_t isa_irq_to_gsi(uint8_t irq, uint16_t* flags_out) {
    if (irq < 16 && g_iso[irq].present) {
        *flags_out = g_iso[irq].flags;
        return g_iso[irq].gsi;
    }
    *flags_out = 0;
    return (uint32_t) irq;
}

static ioapic_t* ioapic_for_gsi(uint32_t gsi) {
    for (size_t i = 0; i < g_ioapic_count; i++) {
        ioapic_t* io = &g_ioapics[i];
        uint32_t start = io->gsi_base;
        uint32_t end = start + io->redir_count;
        if (gsi >= start && gsi < end)
            return io;
    }
    return nullptr;
}

static void ioapic_mask_all(ioapic_t* io) {
    for (uint32_t pin = 0; pin < io->redir_count; pin++) {
        uint8_t lo_reg = (uint8_t) IOREDTBL(pin);
        uint32_t lo = ioapic_read(io, lo_reg);
        lo |= (uint32_t) IOAPIC_REDIR_MASK;
        ioapic_write(io, lo_reg, lo);
    }
}

void ioapic_route_gsi(uint32_t gsi, uint8_t vector, uint8_t dest_lapic_id, uint16_t iso_flags, bool masked) {
    ioapic_t* io = ioapic_for_gsi(gsi);
    ASSERT(io);

    uint32_t pin = gsi - io->gsi_base;
    ASSERT(pin < io->redir_count);

    bool active_low, level;
    decode_iso_flags(iso_flags, &active_low, &level);

    uint64_t entry = 0;
    entry |= (uint64_t) vector;
    entry |= IOAPIC_REDIR_DELIV_FIXED | IOAPIC_REDIR_DEST_PHYSICAL;

    if (active_low)
        entry |= IOAPIC_REDIR_POLARITY_LOW;
    if (level)
        entry |= IOAPIC_REDIR_TRIGGER_LEVEL;
    if (masked)
        entry |= IOAPIC_REDIR_MASK;

    // Destination field (physical) is bits 56..63
    entry |= ((uint64_t) dest_lapic_id) << 56;

    ioapic_write_redir(io, pin, entry);
}

void ioapic_route_isa_irq(uint8_t irq, uint8_t vector, uint8_t dest_lapic_id, bool masked) {
    uint16_t flags;
    uint32_t gsi = isa_irq_to_gsi(irq, &flags);
    ioapic_route_gsi(gsi, vector, dest_lapic_id, flags, masked);
}

void ioapic_init(void) {
    g_ioapic_count = 0;
    for (int i = 0; i < 16; i++)
        g_iso[i] = (iso_map_t) {0};

    struct uacpi_table table;
    ASSERT(uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &table) == UACPI_STATUS_OK);

    const struct acpi_madt* madt = (const struct acpi_madt*) table.ptr;

    const uint8_t* p = (const uint8_t*) madt->entries;
    const uint8_t* end = (const uint8_t*) madt + madt->hdr.length;

    while (p + sizeof(struct acpi_entry_hdr) <= end) {
        const struct acpi_entry_hdr* hdr = (const struct acpi_entry_hdr*) p;
        if (hdr->length < sizeof(*hdr) || p + hdr->length > end)
            break;

        switch (hdr->type) {
            case ACPI_MADT_ENTRY_TYPE_IOAPIC: {
                if (hdr->length >= sizeof(struct acpi_madt_ioapic) && g_ioapic_count < MAX_IOAPICS) {
                    const struct acpi_madt_ioapic* e = (const struct acpi_madt_ioapic*) p;

                    ioapic_map(&g_ioapics[g_ioapic_count], e->address, e->gsi_base);
                    ioapic_mask_all(&g_ioapics[g_ioapic_count]);
                    g_ioapic_count++;
                }
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE: {
                if (hdr->length >= sizeof(struct acpi_madt_interrupt_source_override)) {
                    const struct acpi_madt_interrupt_source_override* e = (const struct acpi_madt_interrupt_source_override*) p;

                    if (e->bus == 0 && e->source < 16) {
                        g_iso[e->source].present = true;
                        g_iso[e->source].gsi = e->gsi;
                        g_iso[e->source].flags = e->flags;
                    }
                }
                break;
            }
            default: break;
        }

        p += hdr->length;
    }

    uacpi_table_unref(&table);
    ASSERT(g_ioapic_count > 0);
    logln(LOG_INFO, "IOAPIC", "Initialized (%lu) IOAPIC(s)", g_ioapic_count);
}
