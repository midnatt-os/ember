#pragma once
#include <stddef.h>

void* heap_alloc(size_t size);
void heap_free(void* obj, size_t size);

void heap_init();
