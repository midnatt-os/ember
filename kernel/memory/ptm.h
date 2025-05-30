#pragma once

#include "memory/vm.h"

void ptm_map(VmAddressSpace* as, uintptr_t vaddr, uintptr_t paddr, VmProtection prot, VmCaching caching, VmPrivilege priv);
void ptm_unmap(VmAddressSpace* as, uintptr_t vaddr);
uintptr_t ptm_virt_to_phys(VmAddressSpace* as, uintptr_t virt_addr);
