#pragma once

#include "mem/vm.h"

#include <stddef.h>

void ptm_map(vm_address_space_t* as, uintptr_t virt_addr, uintptr_t phys_addr, size_t length, vm_prot_t prot, vm_caching_t caching);
void ptm_unmap([[maybe_unused]] vm_address_space_t* as, [[maybe_unused]] uintptr_t addr, [[maybe_unused]] size_t length);
void ptm_map_2mb_special(vm_address_space_t* as, uintptr_t virt_addr, uintptr_t phys_addr, size_t length, vm_prot_t prot, vm_caching_t caching);
uintptr_t ptm_virt_to_phys(vm_address_space_t* as, uintptr_t virt_addr);
