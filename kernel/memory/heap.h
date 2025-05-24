#pragma once

#include <stddef.h>

void* kmalloc(size_t size);
void* krealloc(void* ptr, size_t current_size, size_t new_size);
void kfree(void* ptr);
void heap_init();
