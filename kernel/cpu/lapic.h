#pragma once

#include <stdint.h>

void lapic_eoi();
void lapic_ipi(uint32_t dest_lapic_id, uint8_t vector);
void lapic_timer_oneshot(uint64_t ns, uint8_t vector);
void lapic_timer_periodic(uint64_t ns, uint8_t vector);
void lapic_timer_init();
void lapic_init();
