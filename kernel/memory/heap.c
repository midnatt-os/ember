#include "memory/heap.h"

#include "common/assert.h"
#include "common/log.h"
#include "common/panic.h"
#include "common/util.h"
#include "common/lock/spinlock.h"
#include "lib/mem.h"
#include "memory/vm.h"

#define POOL_SIZE ((size_t) 4096 * 16) // 64 KiB



Spinlock heap_lock = SPINLOCK_NEW;

uint8_t* bump_pool;
uintptr_t next;

static void refill_pool() {
    bump_pool = vm_map_anon(&kernel_as, nullptr, POOL_SIZE, VM_PROT_RW, VM_CACHING_DEFAULT, VM_FLAG_NONE);
    ASSERT(bump_pool != nullptr);
    next = (uintptr_t) bump_pool;
}

void* kmalloc(size_t size) {
    spinlock_acquire(&heap_lock);

    size = ALIGN_UP(size, 8);

    if (next - ((uintptr_t) bump_pool) + size > POOL_SIZE)
        refill_pool();

    uintptr_t addr = next;
    next += size;

    spinlock_release(&heap_lock);

    return (void*) addr;
}

void* krealloc(void* ptr, size_t current_size, size_t new_size) {
    void* new_ptr = kmalloc(new_size);
    if (ptr == nullptr || new_size == 0)
        return new_ptr;

    memclear(new_ptr, new_size);
    memcpy(new_ptr, ptr, current_size > new_size ? current_size : new_size);
    kfree(ptr);

    return new_ptr;
}

void kfree([[maybe_unused]] void* ptr) {}

void heap_init() {
    refill_pool();
    logln(LOG_INFO, "HEAP", "Initialized");
}
