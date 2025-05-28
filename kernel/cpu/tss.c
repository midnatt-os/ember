#include "cpu/tss.h"

#include <stdint.h>



void tss_set_rsp0(Tss* tss, uintptr_t rsp) {
    tss->rsp0_lower = (uint32_t) rsp;
    tss->rsp0_upper = (uint32_t) (rsp >> 32);
}
