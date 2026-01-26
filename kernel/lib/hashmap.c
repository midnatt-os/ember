#include "hashmap.h"

#include "common/assert.h"

#include <stddef.h>

static inline size_t bucket_index(size_t hash, size_t bucket_count) {
    return (size_t) (hash % bucket_count);
}

bool hashmap_init(hashmap_t* map, hashmap_node_t** buckets, size_t bucket_count, hashmap_hash_fn hash_key, hashmap_eq_fn key_eq) {
    map->buckets = buckets;
    map->bucket_count = bucket_count;
    map->size = 0;
    map->hash_key = hash_key;
    map->key_eq = key_eq;

    for (size_t i = 0; i < bucket_count; i++) {
        buckets[i] = nullptr;
    }

    return true;
}

bool hashmap_insert(hashmap_t* map, const void* key, hashmap_node_t* node) {
    ASSERT(node->next == nullptr);
    ASSERT(map);
    ASSERT(node);
    ASSERT(key);

    node->hash = map->hash_key(key);
    const size_t idx = bucket_index(node->hash, map->bucket_count);

    for (hashmap_node_t* it = map->buckets[idx]; it; it = it->next) {
        if (it->hash == node->hash && map->key_eq(it, key)) {
            return false;
        }
    }

    node->next = map->buckets[idx];
    map->buckets[idx] = node;
    map->size++;
    return true;
}

hashmap_node_t* hashmap_find(const hashmap_t* map, const void* key) {
    ASSERT(map);
    ASSERT(key);

    const size_t h = map->hash_key(key);
    const size_t idx = bucket_index(h, map->bucket_count);

    for (hashmap_node_t* it = map->buckets[idx]; it; it = it->next) {
        if (it->hash == h && map->key_eq(it, key)) {
            return it;
        }
    }
    return nullptr;
}

bool hashmap_remove(hashmap_t* map, const void* key, hashmap_node_t** out_node) {
    ASSERT(map);
    ASSERT(key);

    const size_t h = map->hash_key(key);
    const size_t idx = bucket_index(h, map->bucket_count);

    hashmap_node_t** pp = &map->buckets[idx];
    while (*pp) {
        hashmap_node_t* it = *pp;
        if (it->hash == h && map->key_eq(it, key)) {
            *pp = it->next;
            it->next = nullptr;
            map->size--;
            if (out_node) {
                *out_node = it;
            }
            return true;
        }
        pp = &it->next;
    }
    return false;
}
