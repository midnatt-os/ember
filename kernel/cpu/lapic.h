#pragma once

#include <stdint.h>

typedef enum {
    LAPIC_DS_NONE = 0u << 18,
    LAPIC_DS_SELF = 1u << 18,
    LAPIC_DS_ALL = 2u << 18,
    LAPIC_DS_OTHERS = 3u << 18,
} lapic_dest_shorthand_t;

typedef enum {
    LAPIC_DM_FIXED = 0u << 8,
    LAPIC_DM_NMI = 4u << 8,
    LAPIC_DM_INIT = 5u << 8,
    LAPIC_DM_STARTUP = 6u << 8,
} lapic_delivery_mode_t;

void lapic_eoi();
void lapic_timer_one_shot(uint64_t ns, uint8_t vec);
void lapic_timer_stop();

void lapic_send_ipi(uint32_t lapic_id, uint8_t vector, lapic_delivery_mode_t delivery_mode, lapic_dest_shorthand_t shorthand);
void lapic_broadcast_ipi(uint8_t vector, lapic_delivery_mode_t delivery_mode, bool include_self);

void lapic_init();
void lapic_bsp_init();
