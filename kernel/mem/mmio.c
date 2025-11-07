#include "mem/mmio.h"

#include "common/align.h"
#include "mem/page.h"
#include "mem/vm.h"

#include <stdint.h>


void* mmio_map(uintptr_t addr, uintptr_t length) {
    size_t offset = addr % PAGE_SIZE;
    addr -= offset;
    length += offset;

    return (void*) ((uintptr_t) vm_map_direct(&global_as, nullptr, ALIGN_UP(length, PAGE_SIZE), 0, addr, VM_PROT_RW, VM_CACHING_UNCACHED, VM_FLAG_DEFAULT) + offset);
}

void mmio_unmap(void* addr, uintptr_t length) {
    size_t offset = (uintptr_t) addr % PAGE_SIZE;
    addr -= offset;
    length += offset;

    vm_unmap(&global_as, addr, ALIGN_UP(length, PAGE_SIZE));
}
