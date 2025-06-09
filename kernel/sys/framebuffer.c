#include "sys/framebuffer.h"

#include "common/assert.h"
#include "memory/hhdm.h"
#include "memory/vm.h"
#include "common/types.h"



SysFramebuffer fb;

void framebuffer_init(Framebuffer* _fb) {
    fb = (SysFramebuffer) {
        .address = PHYS_FROM_HHDM(_fb->address),
        .width = _fb->width,
        .height = _fb->height,
        .pitch = _fb->pitch,
        .bpp = _fb->bpp,
        .red_mask_size = _fb->red_mask_size,
        .red_mask_shift = _fb->red_mask_shift,
        .green_mask_size = _fb->green_mask_size,
        .green_mask_shift = _fb->green_mask_shift,
        .blue_mask_size = _fb->blue_mask_size,
        .blue_mask_shift = _fb->blue_mask_shift
    };
}

void framebuffer_map(VmAddressSpace* as, SysFramebuffer* new_fb) {
    void* fb_addr = vm_map_direct(
        as,
        nullptr,
        fb.height * fb.pitch,
        (paddr_t) fb.address,
        VM_PROT_RW,
        VM_CACHING_DEFAULT,
        VM_FLAG_NONE
    );

    ASSERT(fb_addr != nullptr);

    *new_fb = fb;
    new_fb->address = fb_addr;
}
