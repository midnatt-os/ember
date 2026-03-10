#pragma once

#include <stdint.h>

void ioapic_route_isa_irq(uint8_t irq, uint8_t vector, uint8_t dest_lapic_id, bool masked);
void ioapic_init();
