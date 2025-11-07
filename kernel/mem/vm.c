#include "mem/vm.h"

#include "common/align.h"
#include "common/asm.h"
#include "common/assert.h"
#include "common/limine_requests.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "lib/container.h"
#include "lib/list.h"
#include "lib/rb.h"
#include "limine.h"
#include "mem/hhdm.h"
#include "mem/page.h"
#include "mem/pmm.h"
#include "mem/ptm.h"

#include <stddef.h>
#include <stdint.h>

#define REGION_END(R) (R->base + R->length)

typedef uint8_t linker_symbol_t[];

extern linker_symbol_t __TEXT_START, __TEXT_END;
extern linker_symbol_t __RODATA_START, __RODATA_END;
extern linker_symbol_t __DATA_START, __DATA_END;
extern linker_symbol_t __BSS_START, __BSS_END;
extern linker_symbol_t __LIMINE_REQ_START, __LIMINE_REQ_END;


vm_address_space_t global_as = {};
list_t region_pool = LIST_NEW;
spinlock_t region_pool_lock = SPINLOCK_NEW;

static rb_value_t region_rb_value(const rb_node_t* n) {
    return CONTAINER_OF(n, vm_region_t, rb_node)->base;
}

static bool regions_mergeable(vm_region_t* a, vm_region_t* b) {
    if (REGION_END(a) != b->base)
        return false;

    if (a->type != b->type)
        return false;

    if (!((a->prot.read == b->prot.read) && (a->prot.write == b->prot.write) && (a->prot.execute == b->prot.execute)))
        return false;

    if (a->caching != b->caching)
        return false;

    switch (a->type) {
        case VM_REGION_TYPE_ANON:
            if (a->type_data.anon.zeroed != b->type_data.anon.zeroed)
                return false;
            break;
        case VM_REGION_TYPE_DIRECT:
            if (a->type_data.direct.phys_addr + a->length != b->type_data.direct.phys_addr)
                return false;
            break;
    }

    return true;
}

/*
 * uintptr_t* addr - If it isn't 0, use it as a hint.
 * bool fixed - hint is absolute. If set, treat addr == 0 as an actual hint.
 */
static bool find_space(vm_address_space_t* as, size_t length, uintptr_t* addr, bool fixed, size_t align) {
    rb_tree_t* t = &as->regions;
    const uintptr_t lo = as->lower_bound;
    const uintptr_t hi = as->upper_bound;

    if (align == 0)
        align = PAGE_SIZE;
    ASSERT(align >= PAGE_SIZE);
    ASSERT((align % PAGE_SIZE) == 0);

    uintptr_t provided = addr ? *addr : 0;
    bool have_hint = (addr && (provided != 0 || fixed));
    uintptr_t hint = have_hint ? provided : 0;

    // 1) Try the hint (don’t auto-adjust if FIXED)
    if (have_hint) {
        if (!fixed && hint < lo)
            hint = lo;

        uintptr_t start = fixed ? hint : ALIGN_UP(hint, align);
        if (fixed && (start % align) != 0)
            return false;

        uintptr_t end = start + length;
        bool overflow = (end < start);
        bool oob = (start < lo) || (hi && end > hi);

        if (!overflow && !oob) {
            rb_node_t* ub = rb_lower_bound(t, end);
            rb_node_t* lb = (ub == t->nil) ? rb_maximum(t, t->root) : rb_predecessor(t, ub);

            uintptr_t prev_end = lo;
            if (lb != t->nil) {
                vm_region_t* L = CONTAINER_OF(lb, vm_region_t, rb_node);
                prev_end = L->base + L->length;
            }

            bool fits = (prev_end <= start);
            if (fits && ub != t->nil) {
                vm_region_t* U = CONTAINER_OF(ub, vm_region_t, rb_node);
                fits = (end <= U->base);
            }
            if (fits) {
                *addr = start;
                return true;
            }
        }

        if (fixed)
            return false;
    }

    // 2) First-fit scan
    uintptr_t prev_end = lo;
    rb_node_t* it = rb_lower_bound(t, lo);
    rb_node_t* pred = (it == t->nil) ? rb_maximum(t, t->root) : rb_predecessor(t, it);
    if (pred != t->nil) {
        vm_region_t* P = CONTAINER_OF(pred, vm_region_t, rb_node);
        uintptr_t pend = P->base + P->length;
        if (pend > prev_end)
            prev_end = pend;
    }

    for (; it != t->nil; it = rb_successor(t, it)) {
        vm_region_t* r = CONTAINER_OF(it, vm_region_t, rb_node);

        uintptr_t start = ALIGN_UP(prev_end, align);
        uintptr_t end = start + length;

        if ((!hi || start <= hi - length) && (end <= r->base) && (start >= prev_end)) {
            *addr = start;
            return true;
        }

        uintptr_t rend = r->base + r->length;
        if (rend > prev_end)
            prev_end = rend;
        if (hi && prev_end > hi - length)
            break;
    }

    // 3) Tail
    uintptr_t start = ALIGN_UP(prev_end, align);
    if (!hi || start <= hi - length) {
        *addr = start;
        return true;
    }

    return false;
}

void region_free(vm_region_t* r) {
    bool prev = spinlock_lock(&region_pool_lock);
    list_append(&region_pool, &r->pool_node);
    spinlock_unlock(&region_pool_lock, prev);
}

static void region_absorb_right(vm_address_space_t* as, vm_region_t* r) {
    rb_tree_t* t = &as->regions;

    while (true) {
        rb_node_t* right_node = rb_lower_bound(t, REGION_END(r));
        if (right_node == t->nil)
            break;

        vm_region_t* r_right = CONTAINER_OF(right_node, vm_region_t, rb_node);
        if (!regions_mergeable(r, r_right))
            break;

        r->length += r_right->length;
        rb_delete(t, right_node);
        region_free(r_right);
    }
}

void region_insert(vm_address_space_t* as, vm_region_t* r) {
    rb_tree_t* t = &as->regions;

    // 1) Merge right
    region_absorb_right(as, r);

    // 2) Get left neighbor (lower bound -> predecessor)
    rb_node_t* lb = rb_lower_bound(t, (rb_value_t) r->base);
    rb_node_t* ln = (lb == t->nil) ? rb_maximum(t, t->root) : rb_predecessor(t, lb);

    if (ln != t->nil) {
        vm_region_t* left = CONTAINER_OF(ln, vm_region_t, rb_node);

        // Left must not overlap 'r'.
        ASSERT(REGION_END(left) <= r->base);

        // If left is exactly contiguous and mergeable, absorb 'r' into left.
        if (REGION_END(left) == r->base && regions_mergeable(left, r)) {
            left->length += r->length;
            region_free(r);

            // After extending left, it may now touch more rights—coalesce them too.
            region_absorb_right(as, left);
            return;
        }
    }

    // 3) No left-merge → insert the (already right-merged) 'r'.
    rb_insert(t, &r->rb_node);
}

static bool prot_equal(vm_prot_t lhs, vm_prot_t rhs) {
    return lhs.read == rhs.read && lhs.write == rhs.write && lhs.execute == rhs.execute;
}

static vm_region_t* region_find(vm_address_space_t* as, uintptr_t addr) {
    rb_tree_t* t = &as->regions;

    rb_node_t* node = rb_lower_bound(t, (rb_value_t) addr);
    if (node != t->nil) {
        vm_region_t* region = CONTAINER_OF(node, vm_region_t, rb_node);
        if (addr >= region->base && addr < region->base + region->length) {
            return region;
        }
    }

    rb_node_t* predecessor = (node == t->nil) ? rb_maximum(t, t->root) : rb_predecessor(t, node);
    if (predecessor != t->nil) {
        vm_region_t* region = CONTAINER_OF(predecessor, vm_region_t, rb_node);
        if (addr >= region->base && addr < region->base + region->length) {
            return region;
        }
    }

    return nullptr;
}

vm_region_t* region_alloc() {
    bool prev_pool = spinlock_lock(&region_pool_lock);

    if (!list_is_empty(&region_pool)) {
        list_node_t* n = list_pop(&region_pool);
        spinlock_unlock(&region_pool_lock, prev_pool);
        return CONTAINER_OF(n, vm_region_t, pool_node);
    }

    spinlock_unlock(&region_pool_lock, prev_pool);

    uintptr_t page = pmm_alloc(PMM_ZERO);
    vm_region_t* new_regions = (vm_region_t*) HHDM(page);

    bool prev = spinlock_lock(&region_pool_lock);
    for (size_t i = 0; i < PAGE_SIZE / sizeof(vm_region_t); i++)
        list_append(&region_pool, &new_regions[i].pool_node);

    vm_region_t* r = CONTAINER_OF(list_pop(&region_pool), vm_region_t, pool_node);

    spinlock_unlock(&region_pool_lock, prev);

    return r;
}

static vm_region_t* region_extract_range(vm_address_space_t* as, vm_region_t* region, uintptr_t start, size_t length, vm_prot_t new_prot) {
    const uintptr_t orig_base = region->base;
    const uintptr_t orig_end = region->base + region->length;
    const uintptr_t target_end = start + length;

    vm_region_type_t type = region->type;
    vm_prot_t old_prot = region->prot;
    vm_caching_t caching = region->caching;
    bool on_demand = region->on_demand;
    bool zeroed = (type == VM_REGION_TYPE_ANON) ? region->type_data.anon.zeroed : false;
    uintptr_t phys_base = (type == VM_REGION_TYPE_DIRECT) ? region->type_data.direct.phys_addr : 0;

    rb_delete(&as->regions, &region->rb_node);

    vm_region_t* target = region;

    if (start > orig_base) {
        size_t left_len = start - orig_base;
        region->base = orig_base;
        region->length = left_len;
        region->type = type;
        region->prot = old_prot;
        region->caching = caching;
        region->on_demand = on_demand;
        if (type == VM_REGION_TYPE_ANON) {
            region->type_data.anon.zeroed = zeroed;
        } else {
            region->type_data.direct.phys_addr = phys_base;
        }

        region_insert(as, region);

        target = region_alloc();
    }

    target->as = as;
    target->base = start;
    target->length = length;
    target->type = type;
    target->prot = new_prot;
    target->caching = caching;
    target->on_demand = on_demand;
    if (type == VM_REGION_TYPE_ANON) {
        target->type_data.anon.zeroed = zeroed;
    } else {
        target->type_data.direct.phys_addr = phys_base + (start - orig_base);
    }

    region_insert(as, target);

    if (target_end < orig_end) {
        vm_region_t* tail = region_alloc();
        tail->as = as;
        tail->base = target_end;
        tail->length = orig_end - target_end;
        tail->type = type;
        tail->prot = old_prot;
        tail->caching = caching;
        tail->on_demand = on_demand;
        if (type == VM_REGION_TYPE_ANON) {
            tail->type_data.anon.zeroed = zeroed;
        } else {
            tail->type_data.direct.phys_addr = phys_base + (target_end - orig_base);
        }
        region_insert(as, tail);
    }

    return target;
}

static void region_map(vm_region_t* region, uintptr_t address, uintptr_t length) {
    ASSERT(address % PAGE_SIZE == 0 && length % PAGE_SIZE == 0);

    switch (region->type) {
        case VM_REGION_TYPE_ANON:
            for (size_t i = 0; i < length; i += PAGE_SIZE) {
                uintptr_t virt = address + i;
                uintptr_t phys = pmm_alloc(region->type_data.anon.zeroed ? PMM_ZERO : PMM_DEFAULT);
                ptm_map(region->as, virt, phys, PAGE_SIZE, region->prot, region->caching);
            }
            break;
        case VM_REGION_TYPE_DIRECT: ptm_map(region->as, address, region->type_data.direct.phys_addr + (address - region->base), length, region->prot, region->caching); break;
    }
}

static void* map_common(vm_address_space_t* as, void* hint, size_t length, size_t align, uintptr_t paddr, vm_prot_t prot, vm_caching_t caching, vm_region_type_t type, uint64_t flags) {
    ASSERT((uintptr_t) hint % PAGE_SIZE == 0);
    ASSERT(length % PAGE_SIZE == 0);
    ASSERT(paddr % PAGE_SIZE == 0);

    bool prev = spinlock_lock(&as->lock);

    uintptr_t addr = (uintptr_t) hint;
    if (!find_space(as, length, &addr, flags & VM_FLAG_FIXED, align)) {
        spinlock_unlock(&as->lock, prev);
        return nullptr;
    }

    vm_region_t* r = region_alloc();

    *r = (vm_region_t) {
        .as = as,
        .base = addr,
        .length = length,
        .type = type,
        .prot = prot,
        .caching = caching,
        .on_demand = (flags & VM_FLAG_DEMAND_PAGED),
    };

    switch (r->type) {
        case VM_REGION_TYPE_ANON:   r->type_data.anon.zeroed = (flags & VM_FLAG_ZERO); break;
        case VM_REGION_TYPE_DIRECT: r->type_data.direct.phys_addr = paddr; break;
    }

    if (!r->on_demand)
        region_map(r, r->base, r->length);

    region_insert(as, r);

    spinlock_unlock(&as->lock, prev);
    return (void*) addr;
}

void* vm_map_anon(vm_address_space_t* as, void* hint, size_t length, size_t align, vm_prot_t prot, vm_caching_t caching, uint64_t flags) {
    return map_common(as, hint, length, align, 0, prot, caching, VM_REGION_TYPE_ANON, flags);
}

void* vm_map_direct(vm_address_space_t* as, void* hint, size_t length, size_t align, uintptr_t paddr, vm_prot_t prot, vm_caching_t caching, uint64_t flags) {
    return map_common(as, hint, length, align, paddr, prot, caching, VM_REGION_TYPE_DIRECT, flags);
}

void vm_unmap([[maybe_unused]] vm_address_space_t* as, [[maybe_unused]] void* base, [[maybe_unused]] size_t length) {
    logln(LOG_WARN, "VM", "vm_unmap stubbed");
}

void vm_protect(vm_address_space_t* as, void* base, size_t length, vm_prot_t prot) {
    if (length == 0)
        return;

    uintptr_t addr = (uintptr_t) base;
    ASSERT(addr % PAGE_SIZE == 0);
    ASSERT(length % PAGE_SIZE == 0);

    bool prev = spinlock_lock(&as->lock);

    size_t remaining = length;
    while (remaining) {
        vm_region_t* region = region_find(as, addr);
        if (!region)
            break;

        uintptr_t region_end = region->base + region->length;
        size_t chunk = region_end - addr;
        if (chunk > remaining)
            chunk = remaining;

        if (!prot_equal(region->prot, prot)) {
            vm_region_t* target = region_extract_range(as, region, addr, chunk, prot);
            if (target) {
                target->prot = prot;
                for (size_t off = 0; off < chunk; off += PAGE_SIZE) {
                    uintptr_t va = addr + off;
                    uintptr_t pa;
                    if (target->type == VM_REGION_TYPE_DIRECT) {
                        pa = target->type_data.direct.phys_addr + (va - target->base);
                    } else {
                        pa = ptm_virt_to_phys(as, va);
                        if (!pa)
                            continue;
                    }
                    ptm_map(as, va, pa, PAGE_SIZE, prot, target->caching);
                    invlpg(va);
                }
            }
        }

        addr += chunk;
        remaining -= chunk;
    }

    spinlock_unlock(&as->lock, prev);
}


void vm_load_as(vm_address_space_t* as) {
    cr3_write(as->cr3);
}

void vm_init() {
    global_as = (vm_address_space_t) {
        .cr3 = pmm_alloc(PMM_ZERO),
        .regions = RB_NEW(region_rb_value),
        .lock = SPINLOCK_NEW,
        .lower_bound = KERNELSPACE_START,
        .upper_bound = KERNELSPACE_END,
    };

    uint64_t* pml4 = (uint64_t*) HHDM(global_as.cr3);
    for (size_t i = 256; i < 512; i++)
        pml4[i] = (pmm_alloc(PMM_ZERO) & 0x000F'FFFF'FFFF'F000) | (1 << 1) | (1 << 0);

    struct limine_executable_address_response* kernel_addr = executable_address_request.response;

    vm_map_direct(&global_as, __TEXT_START, __TEXT_END - __TEXT_START, 0, kernel_addr->physical_base + ((uintptr_t) __TEXT_START - kernel_addr->virtual_base), (vm_prot_t) { .read = true, .execute = true }, VM_CACHING_WRITE_BACK, VM_FLAG_FIXED);
    vm_map_direct(&global_as, __RODATA_START, __RODATA_END - __RODATA_START, 0, kernel_addr->physical_base + ((uintptr_t) __RODATA_START - kernel_addr->virtual_base), (vm_prot_t) { .read = true }, VM_CACHING_WRITE_BACK, VM_FLAG_FIXED);
    vm_map_direct(&global_as, __DATA_START, __DATA_END - __DATA_START, 0, kernel_addr->physical_base + ((uintptr_t) __DATA_START - kernel_addr->virtual_base), VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_FIXED);
    vm_map_direct(&global_as, __BSS_START, __BSS_END - __BSS_START, 0, kernel_addr->physical_base + ((uintptr_t) __BSS_START - kernel_addr->virtual_base), VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_FIXED);
    vm_map_direct(
        &global_as, __LIMINE_REQ_START, __LIMINE_REQ_END - __LIMINE_REQ_START, 0, kernel_addr->physical_base + ((uintptr_t) __LIMINE_REQ_START - kernel_addr->virtual_base), (vm_prot_t) { .read = true }, VM_CACHING_WRITE_BACK, VM_FLAG_FIXED
    );

    struct limine_memmap_response* memmap = memmap_request.response;
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* e = memmap->entries[i];
        // Ignore RESERVED and BAD_MEMORY, map the framebuffer with WC caching.
        switch (e->type) {
            case LIMINE_MEMMAP_RESERVED:    continue;
            case LIMINE_MEMMAP_BAD_MEMORY:  continue;
            case LIMINE_MEMMAP_FRAMEBUFFER: {
                ptm_map_2mb_special(&global_as, HHDM(e->base), e->base, ALIGN_UP(e->length, PAGE_SIZE), VM_PROT_RW, VM_CACHING_WRITE_COMBINE);
                continue;
            }
        }

        ptm_map_2mb_special(&global_as, HHDM(e->base), e->base, e->length, VM_PROT_RW, VM_CACHING_WRITE_BACK);

        vm_region_t* r = region_alloc();
        *r = (vm_region_t) {
            .as = &global_as,
            .base = HHDM(e->base),
            .length = e->length,
            .type = VM_REGION_TYPE_DIRECT,
            .prot = VM_PROT_RW,
            .caching = VM_CACHING_WRITE_BACK,
        };

        region_insert(&global_as, r);
    }

    vm_load_as(&global_as);

    logln(LOG_INFO, "VM", "Initialized");
}
