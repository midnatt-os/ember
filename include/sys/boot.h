#pragma once

#include "stdint.h"

#define MODULE_COUNT 64

/* MEMMAP --> */
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
    MemmapEntry* entries;
    uint64_t size;
} Memmap;
/* <-- MEMMAP */

/* MODULES --> */
typedef struct {
    void* address;
    uint64_t size;
    char* path;
    char* cmdline;
} Module;
/* <-- MODULES */

typedef struct {
    Memmap memmap;
    Module modules[MODULE_COUNT];
} BootInfo;
