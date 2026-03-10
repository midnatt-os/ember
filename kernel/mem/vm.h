#pragma once

#include "common/lock/spinlock.h"
#include "lib/list.h"
#include "lib/rb.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// TODO: grow up, grow down. Check WuX convo

#define KERNELSPACE_START 0xFFFF'8000'0000'0000
#define KERNELSPACE_END (UINT64_MAX - PAGE_SIZE)
#define USERSPACE_START (PAGE_SIZE)
#define USERSPACE_END (((uintptr_t) 1 << 47) - PAGE_SIZE - 1)

#define VM_FLAG_DEFAULT (0)
#define VM_FLAG_FIXED (1 << 0)
#define VM_FLAG_ZERO (1 << 1)
#define VM_FLAG_DEMAND_PAGED (1 << 2)

#define VM_PROT_RW ((vm_prot_t) {.read = true, .write = true})

typedef struct {
    uintptr_t cr3;
    uintptr_t lower_bound;
    uintptr_t upper_bound;

    rb_tree_t regions;
    spinlock_t lock;
} vm_address_space_t;

typedef struct {
    bool read;
    bool write;
    bool execute;
} vm_prot_t;

// vm.h
#include <stdbool.h>
#include <stdint.h>

#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define PROT_EXEC 0x04

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_ANONYMOUS MAP_ANON
#define MAP_NORESERVE 0x4000

#define KNOWN_PROT (PROT_READ | PROT_WRITE | PROT_EXEC)
#define KNOWN_FLAGS (MAP_SHARED | MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE)

static inline vm_prot_t libc_to_vm_prot(int prot_flags) {
    return (vm_prot_t) {
        .read = (prot_flags & PROT_READ) != 0,
        .write = (prot_flags & PROT_WRITE) != 0,
        .execute = (prot_flags & PROT_EXEC) != 0,
    };
}

typedef enum {
    VM_CACHING_WRITE_BACK,
    VM_CACHING_WRITE_THROUGH,
    VM_CACHING_UNCACHED,
    VM_CACHING_UNCACHEABLE,
    VM_CACHING_WRITE_PROTECT,
    VM_CACHING_WRITE_COMBINE
} vm_caching_t;

typedef enum {
    VM_REGION_TYPE_ANON,
    VM_REGION_TYPE_DIRECT
} vm_region_type_t;

typedef struct {
    vm_address_space_t* as;
    uintptr_t base;
    size_t length;

    vm_region_type_t type;
    vm_prot_t prot;
    vm_caching_t caching;
    bool on_demand;

    union {
        struct {
            bool zeroed;
        } anon;
        struct {
            uintptr_t phys_addr;
        } direct;
    } type_data;

    rb_node_t rb_node;
    list_node_t pool_node;
} vm_region_t;

extern vm_address_space_t global_as;

void* vm_map_anon(vm_address_space_t* as, void* hint, size_t length, size_t align, vm_prot_t prot, vm_caching_t caching, uint64_t flags);
void* vm_map_direct(vm_address_space_t* as, void* hint, size_t length, size_t align, uintptr_t paddr, vm_prot_t prot, vm_caching_t caching, uint64_t flags);
void vm_unmap([[maybe_unused]] vm_address_space_t* as, [[maybe_unused]] void* base, [[maybe_unused]] size_t length);
void vm_protect(vm_address_space_t* as, void* base, size_t length, vm_prot_t prot);
void vm_tlb_maybe_flush_local(void);

void vm_load_as(vm_address_space_t* as);
vm_address_space_t* vm_new_address_space();
size_t vm_copy_to(vm_address_space_t* as, uintptr_t dest_vaddr, const void* src, size_t length);

void vm_ap_init();
