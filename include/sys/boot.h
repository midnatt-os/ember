#pragma once

#include "stdint.h"

#define MEMMAP_COUNT 64
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
    MemmapEntry entries[MEMMAP_COUNT];
    uint64_t entry_count;
} Memmap;
/* <-- MEMMAP */

/* MODULES --> */
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

/* <-- MODULES */

typedef struct {
    Memmap memmap;
    Modules modules;
} BootInfo;
