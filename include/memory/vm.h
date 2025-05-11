#pragma once

#include "stddef.h"
#include "common/types.h"
#include "common/lock/spinlock.h"
#include "lib/list.h"
#include "sys/boot.h"

#define VM_FLAG_NONE (0)
#define VM_FLAG_FIXED (1 << 1)
#define VM_FLAG_ZERO (1 << 2)

#define VM_PROT_RW ((VmProtection) { .read = true, .write = true })
#define VM_PROT_RWX ((VmProtection) { .read = true, .write = true, .exec = true })

#define PAGE_SIZE 4096

#define KERNELSPACE_START 0xFFFF'8000'0000'0000
#define KERNELSPACE_END (UINT64_MAX - PAGE_SIZE)
#define USERSPACE_START (PAGE_SIZE)
#define USERSPACE_END (((uintptr_t) 1 << 47) - PAGE_SIZE - 1)

#define REGION_OF(NODE) LIST_ELEMENT_OF(NODE, VmRegion, list_node)

typedef struct {
    bool read    : 1;
    bool write   : 1;
    bool exec    : 1;
} VmProtection;

typedef enum {
    VM_CACHING_DEFAULT,
    VM_CACHING_UNCACHED
} VmCaching;

typedef enum {
    VM_PRIV_KERNEL,
    VM_PRIV_USER
} VmPrivilege;

typedef struct {
    Spinlock cr3_lock;
    Spinlock regions_lock;
    paddr_t cr3;
    List regions;
} VmAddressSpace;

typedef enum {
    VM_REGION_TYPE_ANON,
    VM_REGION_TYPE_DIRECT,
} VmRegionType;

typedef struct {
    VmAddressSpace* as;

    uintptr_t base;
    size_t length;

    VmProtection prot;
    VmCaching caching;
    VmPrivilege priv;

    VmRegionType type;
    union {
        struct { bool zeroed; } anon;
        struct { paddr_t paddr; } direct;
    } metadata;

    ListNode list_node;
} VmRegion;

extern VmAddressSpace kernel_as;

void* vm_map_anon(VmAddressSpace* as, void* hint, size_t length, VmProtection prot, VmCaching caching, uint64_t flags);
void* vm_map_direct(VmAddressSpace* as, void* hint, size_t length, paddr_t phys_address, VmProtection prot, VmCaching caching, uint64_t flags);
void vm_unmap(VmAddressSpace* as, void* address, size_t length);
void vm_load_address_space(VmAddressSpace* as);
void vm_init(KernelAddress kernel_addr, Memmap memmap);
