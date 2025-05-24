#pragma once

#include <stdint.h>

#include "common/lock/spinlock.h"
#include "lib/list.h"

typedef struct {
    uint16_t domain;
    uint8_t bus;
    uint8_t dev;
    uint8_t fn;
    ListNode list_node;
} PciDevice;

extern Spinlock pci_devices_lock;
extern List pci_devices;

uint8_t pci_cfg_read_8(PciDevice* device, uint8_t offset);
uint16_t pci_cfg_read_16(PciDevice* device, uint8_t offset);
uint32_t pci_cfg_read_32(PciDevice* device, uint8_t offset);

void pci_cfg_write_8(PciDevice* device, uint8_t offset, uint8_t data);
void pci_cfg_write_16(PciDevice* device, uint8_t offset, uint16_t data);
void pci_cfg_write_32(PciDevice* device, uint8_t offset, uint32_t data);

void pci_enumerate();
