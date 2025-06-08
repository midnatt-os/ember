#include "lib/hash_table.h"

#include "common/assert.h"
#include "lib/mem.h"
#include "memory/heap.h"
#include <stddef.h>



HashTable* hash_table_create(uint64_t bucket_count, HashFunction hash, EqualityFunction equals) {
    HashTable* ht = kmalloc(sizeof(HashTable));
    HashTableEntry** buckets = kmalloc(sizeof(HashTableEntry*) * bucket_count);

    *ht = (HashTable) {
        .bucket_count = bucket_count,
        .count = 0,
        .hash = hash,
        .equals = equals,
        .buckets = buckets
    };

    for (size_t i = 0; i < bucket_count; i++)
        ht->buckets[i] = nullptr;

    return ht;
}

void hash_table_destroy(HashTable* ht) {
    kfree(ht->buckets);
    kfree(ht);
}

bool hash_table_insert(HashTable* ht, HashTableEntry* entry_to_insert) {
    ASSERT(entry_to_insert != nullptr);

    size_t idx = ht->hash(entry_to_insert) % ht->bucket_count;

    for (HashTableEntry* e = ht->buckets[idx]; e != nullptr; e = e->next) {
        if (ht->equals(entry_to_insert, e))
            return false;
    }

    entry_to_insert->next = ht->buckets[idx];
    ht->buckets[idx] = entry_to_insert;
    ht->count++;
    return true;
}

bool hash_table_remove(HashTable* ht, HashTableEntry* entry_to_remove) {
    ASSERT(entry_to_remove != nullptr);

    size_t idx = ht->hash(entry_to_remove) % ht->bucket_count;
    HashTableEntry** current = &ht->buckets[idx];

    while (*current != nullptr) {
        if (*current == entry_to_remove) {
            *current = entry_to_remove->next;
            entry_to_remove->next = nullptr;

            ht->count--;
            return true;
        }

        current = &(*current)->next;
    }

    return false;
}

HashTableEntry* hash_table_lookup(const HashTable* ht, HashTableEntry* probe) {
    ASSERT(probe != nullptr);

    size_t idx = ht->hash(probe) % ht->bucket_count;

    for (HashTableEntry* e = ht->buckets[idx]; e != nullptr; e = e->next) {
        if (ht->equals(probe, e))
            return e;
    }

    return nullptr;
}
