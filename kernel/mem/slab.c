#include "mem/slab.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/lock/spinlock.h"
#include "lib/container.h"
#include "lib/list.h"
#include "mem/page.h"
#include "mem/vm.h"
#include "sys/init.h"

#include <stddef.h>
#include <stdint.h>

spinlock_t caches_lock = SPINLOCK_NEW;
list_t caches = LIST_NEW;

object_cache_t cache_cache;

static inline slab_t* slab_from_obj(object_cache_t* c, void* obj) {
    uintptr_t u = (uintptr_t) obj;
    uintptr_t base = u - (u % c->slab_size);
    return (slab_t*) (base + c->slab_size - sizeof(slab_t));
}

static slab_t* cache_grow(object_cache_t* cache) {
    void* base = vm_map_anon(&global_as, 0, cache->slab_size, cache->slab_size, VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_DEFAULT);
    ASSERT(base);

    slab_t* s = (slab_t*) ((uint8_t*) base + cache->slab_size - sizeof(slab_t));

    uintptr_t start = (uintptr_t) base;
    uintptr_t end = (uintptr_t) s;

    // size_t obj_sz = ALIGN_UP(cache->object_size, 8);
    size_t capacity = (end - start) / cache->object_size;

    *s = (slab_t) {
        .cache = cache,
        .capacity = capacity,
        .free_count = capacity,
        .freelist = nullptr,
    };

    void* head = nullptr;
    for (size_t i = 0; i < capacity; ++i) {
        uint8_t* p = (uint8_t*) start + i * cache->object_size;
        *(void**) p = head;
        head = p;
    }

    s->freelist = head;

    return s;
}

void* slab_alloc(object_cache_t* cache) {
    ASSERT(cache);
    bool prev = spinlock_lock(&cache->slabs_lock);

alloc:
    if (!list_is_empty(&cache->partial_slabs)) {
        slab_t* s = CONTAINER_OF(list_peek(&cache->partial_slabs), slab_t, list_node);
        void* obj = s->freelist;
        ASSERT(obj);
        s->freelist = *(void**) obj;
        s->free_count--;

        if (s->free_count == 0) {
            list_delete(&cache->partial_slabs, &s->list_node);
            list_append(&cache->full_slabs, &s->list_node);
        }

        spinlock_unlock(&cache->slabs_lock, prev);
        return obj;
    }

    if (!list_is_empty(&cache->empty_slabs)) {
        slab_t* s = CONTAINER_OF(list_pop(&cache->empty_slabs), slab_t, list_node);
        list_prepend(&cache->partial_slabs, &s->list_node);
        goto alloc;
    }

    slab_t* new_slab = cache_grow(cache);
    ASSERT(new_slab->freelist);
    list_prepend(&cache->partial_slabs, &new_slab->list_node);

    goto alloc;
}

void slab_free(object_cache_t* cache, void* obj) {
    ASSERT(cache);
    ASSERT(obj);
    bool prev = spinlock_lock(&cache->slabs_lock);

    slab_t* s = slab_from_obj(cache, obj);
    ASSERT(s->cache == cache);

    bool was_full = (s->free_count == 0);

    *(void**) obj = s->freelist;
    s->freelist = obj;
    s->free_count++;

    if (was_full) {
        list_delete(&cache->full_slabs, &s->list_node);
        list_prepend(&cache->partial_slabs, &s->list_node);
    }
    if (s->free_count == s->capacity) {
        list_delete(&cache->partial_slabs, &s->list_node);
        list_prepend(&cache->empty_slabs, &s->list_node);
    }

    spinlock_unlock(&cache->slabs_lock, prev);
}


object_cache_t* slab_create_cache(const char* name, size_t object_size, size_t slab_size) {
    object_cache_t* new = slab_alloc(&cache_cache);
    *new = (object_cache_t) {
        .name = name,
        .object_size = object_size,
        .slab_size = slab_size,
        .slabs_lock = SPINLOCK_NEW,
        .full_slabs = LIST_NEW,
        .partial_slabs = LIST_NEW,
        .empty_slabs = LIST_NEW,
    };

    bool prev = spinlock_lock(&caches_lock);
    list_append(&caches, &new->list_node);
    spinlock_unlock(&caches_lock, prev);

    return new;
}

INIT_TARGET(slab, INIT_STAGE_EARLY, INIT_SCOPE_BSP, INIT_DEPS("vm")) {
    cache_cache = (object_cache_t) {
        .name = "cache",
        .object_size = sizeof(object_cache_t),
        .slab_size = PAGE_SIZE,
        .slabs_lock = SPINLOCK_NEW,
        .full_slabs = LIST_NEW,
        .partial_slabs = LIST_NEW,
        .empty_slabs = LIST_NEW,
    };

    list_append(&caches, &cache_cache.list_node);
}
