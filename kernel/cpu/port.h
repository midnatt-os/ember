#pragma once

#include <stdint.h>

extern void port_write8(uint16_t port, uint8_t data);
extern void port_write16(uint16_t port, uint16_t data);
extern void port_write32(uint16_t port, uint32_t data);
extern uint8_t port_read8(uint16_t port);
extern uint16_t port_read16(uint16_t port);
extern uint32_t port_read32(uint16_t port);
