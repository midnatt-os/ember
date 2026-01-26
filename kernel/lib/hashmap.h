#pragma once

#include <stddef.h>

typedef struct hashmap_node {
    struct hashmap_node* next;
    size_t hash;
} hashmap_node_t;

typedef size_t (*hashmap_hash_fn)(const void* key);
typedef bool (*hashmap_eq_fn)(const hashmap_node_t* node, const void* key);

typedef struct hashmap {
    hashmap_node_t** buckets;
    size_t bucket_count;
    size_t size;

    hashmap_hash_fn hash_key;
    hashmap_eq_fn key_eq;
} hashmap_t;

bool hashmap_init(hashmap_t* map, hashmap_node_t** buckets, size_t bucket_count, hashmap_hash_fn hash_key, hashmap_eq_fn key_eq);

bool hashmap_insert(hashmap_t* map, const void* key, hashmap_node_t* node);
hashmap_node_t* hashmap_find(const hashmap_t* map, const void* key);
bool hashmap_remove(hashmap_t* map, const void* key, hashmap_node_t** out_node);
