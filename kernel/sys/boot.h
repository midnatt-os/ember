#pragma once

#include "stdint.h"

#define MEMMAP_COUNT 64
#define MODULE_COUNT 64

typedef enum {
    MEMMAP_USABLE,
    MEMMAP_RECLAIMABLE,
    MEMMAP_OTHER
} MemmapEntryType;

typedef struct {
    uintptr_t base;
    uintptr_t length;
    MemmapEntryType type;
} MemmapEntry;

typedef struct {
    MemmapEntry entries[MEMMAP_COUNT];
    uint64_t entry_count;
} Memmap;

typedef struct {
    void* address;
    uint64_t size;
    char* path;
    char* cmdline;
} Module;

typedef struct {
    Module modules[MODULE_COUNT];
    uint64_t module_count;
} Modules;

typedef struct {
    uintptr_t virt_base;
    uintptr_t phys_base;
} KernelAddress;

typedef struct limine_smp_response Smp;
typedef struct limine_smp_info SmpInfo;

typedef struct {
    uintptr_t hhdm_offset;
    Memmap memmap;
    Modules modules;
    KernelAddress kernel_addr;
    uintptr_t rsdp_address;
    Smp* smp;
} BootInfo;
