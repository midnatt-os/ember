#pragma once

#include "common/lock/spinlock.h"
#include "lib/list.h"

#include <stddef.h>

// TODO: Maybe object alignment, in the future even DMA?

typedef struct {
    const char* name;
    list_node_t list_node;
    size_t object_size;
    size_t slab_size; // slab_alignment == slab_size

    spinlock_t slabs_lock;
    list_t full_slabs;
    list_t partial_slabs;
    list_t empty_slabs;
} object_cache_t;

typedef struct {
    object_cache_t* cache;
    list_node_t list_node;
    size_t capacity;
    size_t free_count;
    void* freelist;
} slab_t;

void* slab_alloc(object_cache_t* cache);
void slab_free(object_cache_t* cache, void* obj);
object_cache_t* slab_create_cache(const char* name, size_t object_size, size_t slab_size);
