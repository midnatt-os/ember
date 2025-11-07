#pragma once

#include <stdint.h>

uint8_t mmio_read8(volatile void* src);
uint16_t mmio_read16(volatile void* src);
uint32_t mmio_read32(volatile void* src);
uint64_t mmio_read64(volatile void* src);

void mmio_write8(volatile void* dest, uint8_t value);
void mmio_write16(volatile void* dest, uint16_t value);
void mmio_write32(volatile void* dest, uint32_t value);
void mmio_write64(volatile void* dest, uint64_t value);

void* mmio_map(uintptr_t addr, uintptr_t length);
void mmio_unmap(void* addr, uintptr_t length);
