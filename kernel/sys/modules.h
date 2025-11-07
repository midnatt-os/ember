#pragma once

#include <stddef.h>

typedef struct {
    char* name;

    void* base;
    size_t size;

    void (*init)(void);
    void (*deinit)(void);
} module_t;

bool module_load(const void* elf, size_t size, module_t* out_mod);
