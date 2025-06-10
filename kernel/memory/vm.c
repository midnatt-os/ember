#include "memory/vm.h"

#include "common/assert.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "common/util.h"
#include "cpu/registers.h"
#include "lib/list.h"
#include "lib/math.h"
#include "lib/mem.h"
#include "lib/container.h"
#include "memory/hhdm.h"
#include "memory/pmm.h"
#include "memory/ptm.h"
#include "memory/heap.h"
#include "sched/sched.h"
#include <stddef.h>
#include <stdint.h>

#define MIN(A, B) (A < B ? A : B)
#define REGION_INTERSECTS(BASE1, LENGTH1, BASE2, LENGTH2) ((BASE1) < ((BASE2) + (LENGTH2)) && (BASE2) < ((BASE1) + (LENGTH1)))

typedef uint8_t LinkerSymbol[];

extern LinkerSymbol __TEXT_START, __TEXT_END;
extern LinkerSymbol __RODATA_START, __RODATA_END;
extern LinkerSymbol __DATA_START, __DATA_END;
extern LinkerSymbol __BSS_START, __BSS_END;



const bool VM_LOGGING = false;

VmAddressSpace kernel_as;

List region_pool = LIST_NEW;


static void region_insert(VmAddressSpace* as, VmRegion* region) {
    LIST_FOREACH(as->regions, node) {
        VmRegion* current = LIST_ELEMENT_OF(node, VmRegion, list_node);

        if (region->base < current->base) {
            list_node_prepend(&as->regions, &current->list_node, &region->list_node);
            return;
        }
    }

    list_append(&as->regions, &region->list_node);
}

static uintptr_t find_space(VmAddressSpace* as, uintptr_t hint, size_t length, bool fixed) {
    uintptr_t as_start = as == &kernel_as ? KERNELSPACE_START : USERSPACE_START;
    uintptr_t as_end = as == &kernel_as ? KERNELSPACE_END : USERSPACE_END;

    if (hint) {
        LIST_FOREACH(as->regions, node) {
            VmRegion* r = REGION_OF(node);

            if (!REGION_INTERSECTS(hint, length, r->base, r->length))
                continue;

            if (fixed)
                return 0;

            hint = 0;
            break;
        }

        if (hint != 0)
            return hint;
    }

    uintptr_t candidate = as_start;

    LIST_FOREACH(as->regions, node) {
        VmRegion* r = REGION_OF(node);

        if (REGION_INTERSECTS(candidate, length, r->base, r->length)) {
            candidate = r->base + r->length;
            continue;
        }

        if (candidate + length <= r->base)
            return candidate;
    }

    if (candidate + length <= as_end)
        return candidate;

    return 0;
}

static VmRegion* region_pool_alloc() {
    if (!list_is_empty(&region_pool))
        return REGION_OF(list_pop(&region_pool));

    VmRegion* new_regions = (VmRegion*) HHDM(pmm_alloc(PMM_NONE));
    for (size_t i = 0; i < PAGE_SIZE / sizeof(VmRegion); i++)
        list_append(&region_pool, &new_regions[i].list_node);

    // Finally grab a free page and return it.
    return REGION_OF(list_pop(&region_pool));
}

static void region_pool_free(VmRegion* region) {
    *region = (VmRegion) {}; // TODO: Maybe clear, maybe not?
    list_append(&region_pool, &region->list_node);
}

static void region_map(VmRegion* region) {
    for (size_t i = 0; i < region->length; i += PAGE_SIZE) {
        uintptr_t vaddr = region->base + i;
        paddr_t paddr = 0;
        switch (region->type) {
            case VM_REGION_TYPE_ANON: paddr = pmm_alloc(region->metadata.anon.zeroed ? PMM_ZERO : PMM_NONE); break;
            case VM_REGION_TYPE_DIRECT: paddr = region->metadata.direct.paddr + (vaddr - region->base); break;
        }

        ptm_map(region->as, vaddr, paddr, region->prot, region->caching, region->priv);
    }
}

static void region_unmap(VmRegion* region, uintptr_t address, size_t length) {
    switch (region->type) {
        case VM_REGION_TYPE_ANON: break; // TODO, don't leak page frames lol
        case VM_REGION_TYPE_DIRECT: break; // No need to do anything here.
    }

    for (uintptr_t i = 0; i < length; i += PAGE_SIZE)
        ptm_unmap(region->as, address + i);
}

static void* map_common(VmAddressSpace* as, void* hint, size_t length, paddr_t phys_address, VmProtection prot, VmCaching caching, VmRegionType type, uint64_t flags) {
    uintptr_t address = (uintptr_t) hint;

    ASSERT(address % PAGE_SIZE == 0);
    ASSERT(length % PAGE_SIZE == 0);
    ASSERT(phys_address % PAGE_SIZE == 0);

    spinlock_acquire(&as->regions_lock);

    address = find_space(as, address, length, (flags & VM_FLAG_FIXED) != 0);
    if (!address || ((flags & VM_FLAG_FIXED) && address != (uintptr_t)hint)) {
        spinlock_release(&as->regions_lock);
        return nullptr;
    }

    VmRegion* region = region_pool_alloc();

    *region = (VmRegion) {
        .as = as,
        .base = address,
        .length = length,
        .prot = prot,
        .caching = caching,
        .priv = as == &kernel_as ? VM_PRIV_KERNEL : VM_PRIV_USER,
        .type = type,
    };

    switch (region->type) {
        case VM_REGION_TYPE_ANON: region->metadata.anon.zeroed = (flags & VM_FLAG_ZERO) != 0; break;
        case VM_REGION_TYPE_DIRECT: region->metadata.direct.paddr = phys_address; break;
    }

    region_map(region);
    region_insert(as, region);

    spinlock_release(&as->regions_lock);

    return (void*) region->base;
}

void* vm_map_anon(VmAddressSpace* as, void* hint, size_t length, VmProtection prot, VmCaching caching, uint64_t flags) {
    if (VM_LOGGING) {
        logln(LOG_DEBUG, "VM", "map_anon(hint: %#lx, length: %#lx, prot: %c%c%c, caching: %u, flags: %lu)",
            hint, length,
            prot.read ? 'R' : '-', prot.write ? 'W' : '-', prot.exec ? 'X' : '-',
            caching, flags
        );
    }

    return map_common(as, hint, length, 0, prot, caching, VM_REGION_TYPE_ANON, flags);
}

void* vm_map_direct(VmAddressSpace* as, void* hint, size_t length, paddr_t phys_address, VmProtection prot, VmCaching caching, uint64_t flags) {
    if (VM_LOGGING) {
        logln(LOG_DEBUG, "VM", "map_direct(hint: %#lx, length: %#lx, paddr: %#lx, prot: %c%c%c, caching: %u, flags: %lu)",
            hint, length, phys_address,
            prot.read ? 'R' : '-', prot.write ? 'W' : '-', prot.exec ? 'X' : '-',
            caching, flags
        );
    }

    return map_common(as, hint, length, phys_address, prot, caching, VM_REGION_TYPE_DIRECT, flags);
}

void vm_unmap(VmAddressSpace* as, void* address, size_t length) {
    ASSERT((uintptr_t) address % PAGE_SIZE == 0);
    ASSERT(length % PAGE_SIZE == 0);

    if (as == &kernel_as)
        ASSERT((uintptr_t) address >= KERNELSPACE_START && (uintptr_t) address + length <= KERNELSPACE_END);
    else
        ASSERT((uintptr_t) address >= USERSPACE_START && (uintptr_t) address + length <= USERSPACE_END);

    spinlock_acquire(&as->regions_lock);

    uintptr_t scan_pos = (uintptr_t) address;
    uintptr_t scan_end = scan_pos + length;

    ListNode* node = as->regions.head;

    while (node && scan_pos < scan_end) {
        ListNode* next = node->next;
        VmRegion* r = REGION_OF(node);
        uintptr_t r_end = r->base + r->length;

        // Skip until scan_pos.
        if (r_end <= scan_pos) {
            node = next;
            continue;
        }

        // The range to be unmapped ends before the next region, we're done.
        if (scan_end <= r->base)
            break;

        uintptr_t chunk_start = scan_pos;
        uintptr_t chunk_end = MIN(r_end, scan_end); // Either end of the region or end of the range to be unmapped.
        size_t chunk_length = chunk_end - chunk_start;

        region_unmap(r, chunk_start, chunk_length);

        // Split the region into 0-2 new ones (0 if we unmap the entire region, 1-2 left / right).
        size_t left_length = chunk_start - r->base;
        size_t right_length = r_end - chunk_end;

        list_delete(&as->regions, node);

        // Anything left on the left side?
        if (left_length > 0) {
            r->length = left_length;

            if (node->prev != nullptr)
                list_node_append(&as->regions, node->prev, &r->list_node);
            else
                list_prepend(&as->regions, &r->list_node);
        } else {
            region_pool_free(r);
        }

        // Anything left on the right side?
        if (right_length > 0) {
            VmRegion* new = region_pool_alloc();
            *new = (VmRegion) {
                .as = as,
                .base = chunk_end,
                .length = right_length,
                .prot = r->prot,
                .caching = r->caching,
                .priv = r->priv,
                .type = r->type,
                .metadata = r->metadata
            };
            list_node_append(&as->regions, node->prev, &new->list_node);
        }

        scan_pos = chunk_end;
        node = next;
    }

    spinlock_release(&as->regions_lock);
}

size_t vm_copy_to(VmAddressSpace* dest_as, uintptr_t dest_vaddr, void* src, size_t length) {
        size_t bytes_copied = 0;

        while (bytes_copied < length) {
            uintptr_t va = dest_vaddr + bytes_copied;
            uintptr_t pa = ptm_virt_to_phys(dest_as, va);
            ASSERT(pa != 0);

            size_t page_off      = va & (PAGE_SIZE - 1);
            size_t bytes_in_page = PAGE_SIZE - page_off;
            size_t bytes_left    = length - bytes_copied;
            size_t chunk         = (bytes_in_page < bytes_left) ? bytes_in_page : bytes_left;

            // ptm_virt_to_phys already = frame_base + page_off
            void*       dst_ptr = (void*)HHDM(pa);
            const void* src_ptr = (const uint8_t*)src + bytes_copied;
            memcpy(dst_ptr, src_ptr, chunk);

            bytes_copied += chunk;
        }

        return bytes_copied;
}


size_t vm_copy_from(void* dest, VmAddressSpace* src_as, uintptr_t src_vaddr, size_t length) {
    size_t bytes_copied = 0;
    uint8_t* kd = dest;

    while (bytes_copied < length) {
        uintptr_t va = src_vaddr + bytes_copied;
        uintptr_t pa = ptm_virt_to_phys(src_as, va);  // this already = frame_base + (va % PAGE_SIZE)

        if (pa == 0)  // unmapped
            return bytes_copied;

        size_t page_off   = va & (PAGE_SIZE - 1);
        size_t bytes_in_page = PAGE_SIZE - page_off;
        size_t bytes_left    = length - bytes_copied;
        size_t chunk         = (bytes_in_page < bytes_left) ? bytes_in_page : bytes_left;

        const void* src_ptr = (const void*)HHDM(pa);
        memcpy(&kd[bytes_copied], src_ptr, chunk);

        bytes_copied += chunk;
    }

    return bytes_copied;
}

void vm_clone_address_space(VmAddressSpace* dest_as) {
    VmAddressSpace* src_as = sched_get_current_process()->as;

    spinlock_acquire(&src_as->regions_lock);
    LIST_FOREACH(src_as->regions, n) {
        VmRegion* region = CONTAINER_OF(n, VmRegion, list_node);

        VmRegion* new_region = kmalloc(sizeof(VmRegion));

        *new_region = *region;
        new_region->list_node = (ListNode) {};
        new_region->as = dest_as;

        region_map(new_region);
        region_insert(dest_as, new_region);

        switch (region->type) {
            case VM_REGION_TYPE_DIRECT: break;
            case VM_REGION_TYPE_ANON: {
                vm_copy_to(dest_as, new_region->base, (void*) region->base, region->length);
            }
        }
    }

    spinlock_release(&src_as->regions_lock);
}

void vm_load_address_space(VmAddressSpace* as) {
    write_cr3(as->cr3);
}

VmAddressSpace* vm_create_address_space() {
    VmAddressSpace* as = kmalloc(sizeof(VmAddressSpace));

    *as = (VmAddressSpace) {
        .cr3_lock = SPINLOCK_NEW,
        .regions_lock = SPINLOCK_NEW,
        .cr3 = pmm_alloc(PMM_ZERO),
        .regions = LIST_NEW
    };

    memcpy((void *) HHDM(as->cr3 + 256 * sizeof(uint64_t)), (void *) HHDM(kernel_as.cr3 + 256 * sizeof(uint64_t)), 256 * sizeof(uint64_t));

    return as;
}

void vm_init(KernelAddress kernel_addr, Memmap memmap) {
    kernel_as = (VmAddressSpace) {
        .cr3_lock = SPINLOCK_NEW,
        .regions_lock = SPINLOCK_NEW,
        .cr3 = pmm_alloc(PMM_ZERO),
        .regions = LIST_NEW
    };

    uint64_t* pml4 = (uint64_t*) HHDM(kernel_as.cr3);
    for (size_t i = 256; i < 512; i++)
        pml4[i] = (pmm_alloc(PMM_ZERO) & 0x000F'FFFF'FFFF'F000) | (1 << 1) | (1 << 0); // RW, P

    vm_map_direct(
        &kernel_as,
        __TEXT_START,
        __TEXT_END - __TEXT_START,
        kernel_addr.phys_base + ((paddr_t) __TEXT_START - kernel_addr.virt_base),
        (VmProtection) { .read = true, .exec = true },
        VM_CACHING_DEFAULT,
        VM_FLAG_FIXED
    );

    vm_map_direct(
        &kernel_as,
        __RODATA_START,
        __RODATA_END - __RODATA_START,
        kernel_addr.phys_base + ((paddr_t) __RODATA_START - kernel_addr.virt_base),
        (VmProtection) { .read = true },
        VM_CACHING_DEFAULT,
        VM_FLAG_FIXED
    );

    vm_map_direct(
        &kernel_as,
        __DATA_START,
        __DATA_END - __DATA_START,
        kernel_addr.phys_base + ((paddr_t) __DATA_START - kernel_addr.virt_base),
        VM_PROT_RW,
        VM_CACHING_DEFAULT,
        VM_FLAG_FIXED
    );

    vm_map_direct(
        &kernel_as,
        __BSS_START,
        __BSS_END - __BSS_START,
        kernel_addr.phys_base + ((paddr_t) __BSS_START - kernel_addr.virt_base),
        VM_PROT_RW,
        VM_CACHING_DEFAULT,
        VM_FLAG_FIXED
    );

    for (size_t i = 0; i < memmap.entry_count; i++) {
        MemmapEntry entry = memmap.entries[i];

        vm_map_direct(
            &kernel_as,
            (void*) HHDM(entry.base),
            entry.length,
            entry.base,
            VM_PROT_RW,
            VM_CACHING_DEFAULT,
            VM_FLAG_FIXED
        );
    }

    vm_load_address_space(&kernel_as);

    logln(LOG_INFO, "VM", "Initialized");
}
