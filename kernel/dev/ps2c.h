#pragma once

#include <stdint.h>

typedef enum {
    PS2_KB_PORT = 0,
    PS2_MOUSE_PORT = 1
} ps2_port_t;

bool ps2c_pop_kbd(uint8_t* out_byte);
bool ps2c_pop_aux(uint8_t* out_byte);

bool ps2c_send(ps2_port_t port, uint8_t byte);

void ps2c_init();
