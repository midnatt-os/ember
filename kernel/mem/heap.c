#include "mem/heap.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/log.h"
#include "mem/page.h"
#include "mem/slab.h"
#include "sys/init.h"

#include <stddef.h>

#define CACHE_8X_COUNT (sizeof(cache_sizes_8x) / sizeof(*cache_sizes_8x))
#define CACHE_128X_COUNT (sizeof(cache_sizes_128x) / sizeof(*cache_sizes_128x))
#define CACHE_OTHER_COUNT (sizeof(cache_sizes_other) / sizeof(*cache_sizes_other))


static const char* const cache_names_8x[] = {
    "heap-8",
    "heap-16",
    "heap-24",
    "heap-32",
    "heap-40",
    "heap-48",
    "heap-56",
    "heap-64",
    "heap-72",
    "heap-80",
    "heap-88",
    "heap-96",
    "heap-104",
    "heap-112",
    "heap-120",
};

static const size_t cache_sizes_8x[] = {
    8,
    16,
    24,
    32,
    40,
    48,
    56,
    64,
    72,
    80,
    88,
    96,
    104,
    112,
    120,
};

static const char* const cache_names_128x[] = {
    "heap-128",
    "heap-256",
    "heap-384",
    "heap-512",
};

static const size_t cache_sizes_128x[] = {
    128,
    256,
    384,
    512,
};

static const char* const cache_names_other[] = {
    "heap-1024",
    "heap-2048",
};

static const size_t cache_sizes_other[] = {
    1024,
    2048,
};

static object_cache_t* caches_8x[CACHE_8X_COUNT];
static object_cache_t* caches_128x[CACHE_128X_COUNT];
static object_cache_t* caches_other[CACHE_OTHER_COUNT];

static object_cache_t* choose_cache(size_t size) {
    size = ALIGN_UP(size, 8);

    if (size <= cache_sizes_8x[CACHE_8X_COUNT - 1])
        return caches_8x[DIV_CEIL(size, 8) - 1];
    else if (size <= cache_sizes_128x[CACHE_128X_COUNT - 1])
        return caches_128x[DIV_CEIL(size, 128) - 1];

    for (size_t i = 0; i < CACHE_OTHER_COUNT; i++)
        if (cache_sizes_other[i] >= size)
            return caches_other[i];

    ASSERT_UNREACHABLE();
}

void* heap_alloc([[maybe_unused]] size_t size) {
    ASSERT(size > 0);
    ASSERT(size <= cache_sizes_other[CACHE_OTHER_COUNT - 1]);
    return slab_alloc(choose_cache(size));
}

void heap_free(void* obj, size_t size) {
    ASSERT(obj != nullptr);
    ASSERT(size > 0);
    ASSERT(size <= cache_sizes_other[CACHE_OTHER_COUNT - 1]);
    slab_free(choose_cache(size), obj);
}

void heap_init() {
    for (size_t i = 0; i < CACHE_8X_COUNT; i++)
        caches_8x[i] = slab_create_cache(cache_names_8x[i], cache_sizes_8x[i], PAGE_SIZE);

    for (size_t i = 0; i < CACHE_128X_COUNT; i++)
        caches_128x[i] = slab_create_cache(cache_names_128x[i], cache_sizes_128x[i], PAGE_SIZE);

    for (size_t i = 0; i < CACHE_OTHER_COUNT; i++)
        caches_other[i] = slab_create_cache(cache_names_other[i], cache_sizes_other[i], 2 * PAGE_SIZE);

    logln(LOG_INFO, "HEAP", "Initialized");
}

INIT_TARGET(heap, INIT_STAGE_EARLY, INIT_SCOPE_BSP, INIT_DEPS("slab")) {
    heap_init();
}
