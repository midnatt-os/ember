#pragma once

#include "memory/vm.h"
#include "sys/boot.h"

typedef struct {
    void*    address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t  red_mask_size;
    uint8_t  red_mask_shift;
    uint8_t  green_mask_size;
    uint8_t  green_mask_shift;
    uint8_t  blue_mask_size;
    uint8_t  blue_mask_shift;
} SysFramebuffer;

extern SysFramebuffer fb;

void framebuffer_init(Framebuffer* _fb);
void framebuffer_map(VmAddressSpace* as, SysFramebuffer* new_fb);
