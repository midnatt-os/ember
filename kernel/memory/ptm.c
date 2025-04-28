#include "memory/ptm.h"

#include "common/log.h"
#include "memory/hhdm.h"
#include "memory/pmm.h"

#define VADDR_TO_INDEX(VADDR, LEVEL) (((VADDR) >> ((LEVEL) * 9 + 3)) & 0x1FF)
#define ADDR_MASK ((uint64_t) 0x000F'FFFF'FFFF'F000)

#define FLAG_PRESENT (1 << 0)
#define FLAG_RW      (1 << 1)
#define FLAG_KERNEL  (0 << 2)
#define FLAG_USER    (1 << 2)
#define FLAG_PWT     (1 << 3)
#define FLAG_PCD     (1 << 4)
#define FLAG_PAT     (1 << 7)
#define FLAG_NX      ((uint64_t) 1 << 63)



extern VmAddressSpace kernel_as;

static uint64_t priv_to_flags(VmPrivilege priv) {
    switch (priv) {
        case VM_PRIV_USER: return FLAG_USER;
        default: return FLAG_KERNEL;
    }
}

static uint64_t caching_to_flags(VmCaching caching) {
    // TODO: Go over these, maybe when PAT?
    switch (caching) {
        case VM_CACHING_UNCACHED: return FLAG_PCD;
        default: return 0;
    }
}

void ptm_map(VmAddressSpace* as, uintptr_t vaddr, uintptr_t paddr, VmProtection prot, VmCaching caching, VmPrivilege priv) {
    spinlock_acquire(&as->cr3_lock);

    uint64_t* table = (uint64_t*) HHDM(as->cr3);

    for (int i = 4; i > 1; i--) {
        uint64_t index = VADDR_TO_INDEX(vaddr, i);
        uint64_t entry = table[index];

        if ((entry & FLAG_PRESENT) == 0) {
            entry = FLAG_PRESENT | (pmm_alloc(PMM_ZERO) & ADDR_MASK);
            if (!prot.exec) entry |= FLAG_NX;
        } else {
            if (prot.exec) entry &= ~FLAG_NX;
        }

        if (prot.write) entry |= FLAG_RW;

        entry |= priv_to_flags(priv);

        __atomic_store(&table[index], &entry, __ATOMIC_SEQ_CST);
        uintptr_t test = table[index] & ADDR_MASK;
        table = (uint64_t*) HHDM(test);
    }

    uint64_t index = VADDR_TO_INDEX(vaddr, 1);
    uint64_t entry = FLAG_PRESENT | (paddr & ADDR_MASK) | priv_to_flags(priv) | caching_to_flags(caching);

    if(prot.write) entry |= FLAG_RW;
    if(!prot.exec) entry |= FLAG_NX;
    if(as != &kernel_as) entry |= FLAG_USER;

    __atomic_store(&table[index], &entry, __ATOMIC_SEQ_CST);

    //tlb_shootdown();

    spinlock_release(&as->cr3_lock);
}

void ptm_unmap(VmAddressSpace* as, uintptr_t vaddr) {
    spinlock_acquire(&as->cr3_lock);

    uint64_t* table = (uint64_t*) HHDM(as->cr3);

    for (int i = 4; i > 1; i--) {
        uint64_t index = VADDR_TO_INDEX(vaddr, i);

        if (table[index] & FLAG_PRESENT) {
            spinlock_release(&as->cr3_lock);
            return;
        }

        table = (uint64_t*) HHDM(table[i] & ADDR_MASK);
    }

    __atomic_store_n(&table[VADDR_TO_INDEX(vaddr, 1)], 0, __ATOMIC_SEQ_CST);

    // tlb_shootdown();

    spinlock_release(&as->cr3_lock);
}
