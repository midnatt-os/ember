#include "mem/ptm.h"

#include "common/asm.h"
#include "common/assert.h"
#include "common/log.h"
#include "mem/hhdm.h"
#include "mem/page.h"
#include "mem/pmm.h"
#include "mem/vm.h"

#include <stddef.h>
#include <stdint.h>

#define VADDR_TO_INDEX(VADDR, LEVEL) (((VADDR) >> ((LEVEL) * 9 + 3)) & 0x1FF)
#define ADDR_MASK ((uint64_t) 0x000F'FFFF'FFFF'F000)
#define ADDR_MASK_2MB 0x000FFFFFFFFFE00000
#define PAGE_OFFSET_MASK (PAGE_SIZE - 1)

#define PAGE_PRESENT (1 << 0)
#define PAGE_PS (1 << 7)
#define PAGE_RW (1 << 1)
#define PAGE_KERNEL (0 << 2)
#define PAGE_USER (1 << 2)
#define PAGE_PWT (1 << 3)
#define PAGE_PCD (1 << 4)
#define PAGE_PAT (1 << 7)
#define PAGE_PAT_LARGE (1 << 12)
#define PAGE_NX ((uint64_t) 1 << 63)


static uint64_t* table_walk_create(uint64_t* table, uintptr_t vaddr, int level, vm_prot_t prot, bool is_kernel) {
    uint64_t index = VADDR_TO_INDEX(vaddr, level);
    uint64_t entry = table[index];

    if (!(entry & PAGE_PRESENT)) {
        uintptr_t new_page = pmm_alloc(PMM_ZERO);
        entry = (new_page & ADDR_MASK) | PAGE_PRESENT;
        if (!prot.execute)
            entry |= PAGE_NX;
    } else {
        if (prot.execute)
            entry &= ~PAGE_NX;
    }

    if (prot.write)
        entry |= PAGE_RW;
    entry |= is_kernel ? PAGE_KERNEL : PAGE_USER;

    __atomic_store(&table[index], &entry, __ATOMIC_SEQ_CST);

    uintptr_t next_table_phys = entry & ADDR_MASK;
    return (uint64_t*) HHDM(next_table_phys);
}

/*static bool table_is_empty(const uint64_t* table) {
    for (size_t i = 0; i < 512; i++) {
        if (table[i] & PAGE_PRESENT)
            return false;
    }
    return true;
}*/


static uint64_t caching_to_flags(vm_caching_t caching) {
    switch (caching) {
        case VM_CACHING_WRITE_BACK:    return 0;
        case VM_CACHING_WRITE_THROUGH: return PAGE_PWT;
        case VM_CACHING_UNCACHED:      return PAGE_PCD | PAGE_PWT;
        case VM_CACHING_UNCACHEABLE:   return PAGE_PCD;
        case VM_CACHING_WRITE_PROTECT: return PAGE_PAT | PAGE_PCD;
        case VM_CACHING_WRITE_COMBINE: return PAGE_PAT | PAGE_PWT;
        default:                       return 0; // fallback to WB
    }
}

static uint64_t caching_to_flags_2mb(vm_caching_t caching) {
    switch (caching) {
        case VM_CACHING_WRITE_BACK:    return 0;
        case VM_CACHING_WRITE_THROUGH: return PAGE_PWT;
        case VM_CACHING_UNCACHED:      return PAGE_PCD | PAGE_PWT;
        case VM_CACHING_UNCACHEABLE:   return PAGE_PCD;
        case VM_CACHING_WRITE_PROTECT: return PAGE_PAT_LARGE | PAGE_PCD;
        case VM_CACHING_WRITE_COMBINE: return PAGE_PAT_LARGE | PAGE_PWT;
        default:                       return 0; // fallback to WB
    }
}

static void map_2mb(vm_address_space_t* as, uintptr_t virt_addr, uintptr_t phys_addr, size_t length, vm_prot_t prot, vm_caching_t caching) {
    ASSERT((virt_addr % (2 * 1024 * 1024)) == 0);
    ASSERT((phys_addr % (2 * 1024 * 1024)) == 0);
    ASSERT((length % (2 * 1024 * 1024)) == 0);

    bool is_kernel = as == &global_as;

    uint64_t* pml4 = (uint64_t*) HHDM(as->cr3);
    for (size_t off = 0; off < length; off += 2 * 1024 * 1024) {
        uintptr_t va = virt_addr + off;
        uintptr_t pa = phys_addr + off;

        uint64_t* table = pml4;
        for (int level = 4; level > 2; --level)
            table = table_walk_create(table, va, level, prot, is_kernel);

        uint64_t idx = VADDR_TO_INDEX(va, 2);

        uint64_t entry = (pa & ADDR_MASK_2MB) | PAGE_PRESENT | PAGE_PS;

        if (prot.write)
            entry |= PAGE_RW;
        if (!prot.execute)
            entry |= PAGE_NX;

        entry |= is_kernel ? PAGE_KERNEL : PAGE_USER;
        entry |= caching_to_flags_2mb(caching);

        __atomic_store(&table[idx], &entry, __ATOMIC_SEQ_CST);

        invlpg(va);
    }
}

void ptm_map(vm_address_space_t* as, uintptr_t virt_addr, uintptr_t phys_addr, size_t length, vm_prot_t prot, vm_caching_t caching) {
    bool is_kernel = as == &global_as;

    for (size_t offset = 0; offset < length; offset += 0x1000) {
        uintptr_t va = virt_addr + offset;
        uintptr_t pa = phys_addr + offset;

        uint64_t* table = (uint64_t*) HHDM(as->cr3);
        for (int i = 4; i > 1; i--)
            table = table_walk_create(table, va, i, prot, is_kernel);

        uint64_t index = VADDR_TO_INDEX(va, 1);

        uint64_t entry = (pa & ADDR_MASK) | PAGE_PRESENT;

        if (prot.write)
            entry |= PAGE_RW;
        if (!prot.execute)
            entry |= PAGE_NX;

        entry |= is_kernel ? PAGE_KERNEL : PAGE_USER;
        entry |= caching_to_flags(caching);

        __atomic_store(&table[index], &entry, __ATOMIC_SEQ_CST);
    }
}

void ptm_unmap([[maybe_unused]] vm_address_space_t* as, [[maybe_unused]] uintptr_t addr, [[maybe_unused]] size_t length) {
    logln(LOG_WARN, "PTM", "ptm_unmap stubbed");
}


void ptm_map_2mb_special(vm_address_space_t* as, uintptr_t virt_addr, uintptr_t phys_addr, size_t length, vm_prot_t prot, vm_caching_t caching) {
    const size_t SZ2M = 2 * 1024 * 1024;
    const size_t SZ4K = 4096;

    while (length && ((virt_addr | phys_addr) & (SZ2M - 1))) {
        size_t step = SZ4K;
        ptm_map(as, virt_addr, phys_addr, step, prot, caching);
        virt_addr += step;
        phys_addr += step;
        length -= step;
    }

    size_t mid = length & ~(SZ2M - 1);
    if (mid)
        map_2mb(as, virt_addr, phys_addr, mid, prot, caching), virt_addr += mid, phys_addr += mid, length -= mid;

    while (length) {
        size_t step = SZ4K;
        ptm_map(as, virt_addr, phys_addr, step, prot, caching);
        virt_addr += step;
        phys_addr += step;
        length -= step;
    }
}

uintptr_t ptm_virt_to_phys(vm_address_space_t* as, uintptr_t virt_addr) {
    uint64_t* table = (uint64_t*) HHDM(as->cr3);

    for (int level = 4; level > 1; level--) {
        uint64_t idx = VADDR_TO_INDEX(virt_addr, level);
        uint64_t entry = table[idx];

        if (!(entry & PAGE_PRESENT))
            return 0;

        uint64_t next_phys = entry & ADDR_MASK;
        table = (uint64_t*) HHDM(next_phys);
    }

    uint64_t idx = VADDR_TO_INDEX(virt_addr, 1);
    uint64_t entry = table[idx];

    if (!(entry & PAGE_PRESENT))
        return 0;

    return (entry & ADDR_MASK) | (virt_addr & PAGE_OFFSET_MASK);
}
