#pragma once

#include <stddef.h>
#include <stdint.h>


typedef struct HashTableEntry {
    struct HashTableEntry* next;
} HashTableEntry;

typedef size_t (*HashFunction)(const HashTableEntry* entry);
typedef bool (*EqualityFunction)(const HashTableEntry* a, const HashTableEntry* b);

typedef struct {
    uint64_t bucket_count;
    uint64_t count;
    HashFunction hash;
    EqualityFunction equals;
    HashTableEntry** buckets;
} HashTable;

HashTable* hash_table_create(uint64_t bucket_count, HashFunction hash, EqualityFunction equals);
void hash_table_destroy(HashTable* ht);

bool hash_table_insert(HashTable* ht, HashTableEntry* entry_to_insert);
bool hash_table_remove(HashTable* ht, HashTableEntry* entry_to_remove);
HashTableEntry* hash_table_lookup(const HashTable* ht, HashTableEntry* probe);
