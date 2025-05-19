#include "dev/pci.h"

#include <uacpi/tables.h>

#include "common/assert.h"
#include <uacpi/acpi.h>

#include "common/log.h"
#include "memory/heap.h"
#include "memory/hhdm.h"
#include "memory/vm.h"

#define INVALID_VENDOR 0xFFFF

typedef struct [[gnu::packed]] {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t revision_id;
    uint8_t program_interface;
    uint8_t sub_class;
    uint8_t class;
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
} PciDeviceHeader;

typedef struct [[gnu::packed]] {
    PciDeviceHeader device_header;

    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;

    uint32_t cardbus_cis_pointer;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t expansion_rom_base_address;
    uint8_t capabilities_pointer;
    uint8_t rsv0;
    uint16_t rsv1;
    uint32_t rsv2;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint8_t min_grant;
    uint8_t max_latency;
} PciHeader0;

typedef struct [[gnu::packed]] {
    PciDeviceHeader device_header;

    uint32_t bar0;
    uint32_t bar1;

    uint8_t primary_bus;
    uint8_t secondary_bus;
    uint8_t subordinate_bus;
    uint8_t secondary_latency_timer;
    uint8_t io_base_lower;
    uint8_t io_limit_lower;
    uint16_t secondary_status;
    uint16_t memory_base_lower;
    uint16_t memory_limit_lower;
    uint16_t prefetchable_memory_base;
    uint16_t prefetchable_memory_limit;
    uint32_t prefetchable_memory_base_upper;
    uint32_t prefetchable_memory_limit_upper;
    uint16_t io_base_upper;
    uint16_t io_limit_upper;
    uint8_t capability_pointer;
    uint8_t rsv0;
    uint16_t rsv1;
    uint32_t expansion_rom_base;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint16_t bridge_control;
} PciHeader1;

typedef struct {
    uintptr_t ecam_base;
    uint16_t domain_index;
    uint8_t start_bus;
    uint8_t end_bus;
} PcieDomainEntry;



static PcieDomainEntry* domains;

Spinlock pci_devices_lock = SPINLOCK_NEW;
List pci_devices = LIST_NEW;

static uint32_t pcie_read(PciDevice* device, uint8_t offset, uint8_t size) {
    uint32_t register_offset = offset;
    register_offset |= (uint32_t) (device->bus - domains[device->domain].start_bus) << 20;
    register_offset |= (uint32_t) (device->dev & 0x1F) << 15;
    register_offset |= (uint32_t) (device->fn & 0x7) << 12;
    volatile void *address = (void *) HHDM(domains[device->domain].ecam_base) + register_offset;

    switch(size) {
        case 4: return *(volatile uint32_t *) address;
        case 2: return *(volatile uint16_t *) address;
        case 1: return *(volatile uint8_t *) address;
        default: ASSERT(true); return 0;
    }
}

static void pcie_write(PciDevice* device, uint8_t offset, uint8_t size, uint32_t value) {
    uint32_t register_offset = offset;
    register_offset |= (uint32_t) (device->bus - domains[device->domain].start_bus) << 20;
    register_offset |= (uint32_t) (device->dev & 0x1F) << 15;
    register_offset |= (uint32_t) (device->fn & 0x7) << 12;
    volatile void *address = (void *) HHDM(domains[device->domain].ecam_base) + register_offset;

    switch(size) {
        case 4: *(volatile uint32_t *) address = (uint32_t) value; return;
        case 2: *(volatile uint16_t *) address = (uint16_t) value; return;
        case 1: *(volatile uint8_t *) address = (uint8_t) value; return;
        default: ASSERT(true);
    }
}

uint8_t pci_cfg_read_8(PciDevice* device, uint8_t offset) {
    return (uint8_t) pcie_read(device, offset, 1);
}
uint16_t pci_cfg_read_16(PciDevice* device, uint8_t offset) {
    return (uint16_t) pcie_read(device, offset, 2);
}
uint32_t pci_cfg_read_32(PciDevice* device, uint8_t offset) {
    return (uint32_t) pcie_read(device, offset, 4);
}

void pci_cfg_write_8(PciDevice* device, uint8_t offset, uint8_t data) {
    pcie_write(device, offset, 1, data);
}
void pci_cfg_write_16(PciDevice* device, uint8_t offset, uint16_t data) {
    pcie_write(device, offset, 2, data);
}
void pci_cfg_write_32(PciDevice* device, uint8_t offset, uint32_t data) {
    pcie_write(device, offset, 4, data);
}

static void enumerate_bus(uint16_t domain, uint8_t bus);

static void check_function(uint16_t domain, uint8_t bus, uint8_t dev, uint8_t fn) {
    PciDevice* device = kmalloc(sizeof(PciDevice));
    *device = (PciDevice) {
        .domain = domain,
        .bus = bus,
        .dev = dev,
        .fn = fn
    };

    uint16_t vendor_id = pci_cfg_read_16(device, offsetof(PciDeviceHeader, vendor_id));
    if (vendor_id == INVALID_VENDOR) {
        kfree(device);
        return;
    }

    list_append(&pci_devices, &device->list_node);

    uint8_t class = pci_cfg_read_8(device, offsetof(PciDeviceHeader, class));
    uint8_t sub_class = pci_cfg_read_8(device, offsetof(PciDeviceHeader, sub_class));
    if (class == 0x6 && sub_class == 0x4)
        enumerate_bus(domain, pci_cfg_read_8(device, offsetof(PciHeader1, secondary_bus)));
}

static void enumerate_bus(uint16_t domain, uint8_t bus) {
    for (uint8_t dev = 0; dev < 32; dev++) {
        PciDevice device = { .domain = domain, .bus = bus, .dev = dev };

        uint16_t vendor_id = pci_cfg_read_16(&device, offsetof(PciDeviceHeader, vendor_id));
        if (vendor_id == INVALID_VENDOR)
            continue;

        bool has_multi_fn = pci_cfg_read_8(&device, offsetof(PciDeviceHeader, header_type))  & (1 << 7);

        if (has_multi_fn)
            for (uint8_t fn = 0; fn < 8; fn++) check_function(domain, bus, dev, fn);
        else
            check_function(domain, bus, dev, 0);
    }
}

static void enumerate_domain(size_t i) {
    PcieDomainEntry* domain = &domains[i];
    enumerate_bus(domain->domain_index, domain->start_bus);
}

void pci_enumerate() {
    uacpi_table mcfg_table;
    ASSERT(uacpi_table_find_by_signature(ACPI_MCFG_SIGNATURE, &mcfg_table) == UACPI_STATUS_OK);

    struct acpi_mcfg* mcfg = mcfg_table.ptr;
    size_t entry_count = (mcfg->hdr.length - (sizeof(struct acpi_sdt_hdr) + 8)) / sizeof(struct acpi_mcfg_allocation);

    domains = kmalloc(sizeof(PcieDomainEntry) * entry_count);

    for (size_t i = 0; i < entry_count; i++) {
        struct acpi_mcfg_allocation entry = mcfg->entries[i];

        domains[i] = (PcieDomainEntry) {
            .ecam_base = entry.address,
            .domain_index = i,
            .start_bus = entry.start_bus,
            .end_bus = entry.end_bus
        };

        enumerate_domain(i);
    }

    uacpi_table_unref(&mcfg_table);
    logln(LOG_INFO, "PCI", "Enumeration successful, found %d devices", pci_devices.count);
}
