#pragma once

#include <stdint.h>

void ioapic_redirect_irq(uint8_t irq, uint8_t vector, uint8_t lapic_id, bool enabled);
void ioapic_init();
