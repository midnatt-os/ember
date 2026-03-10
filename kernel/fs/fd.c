#include "fs/fd.h"

#include "common/assert.h"
#include "common/errno.h"
#include "fs/file.h"
#include "mem/page.h"
#include "mem/slab.h"

object_cache_t* fd_table_cache = nullptr;

fd_table_t* fd_table_alloc() {
    fd_table_t* table = slab_alloc(fd_table_cache);

    for (int i = 0; i < MAX_FDS; i++) {
        table->entries[i].file = NULL;
        table->entries[i].flags = 0;
    }

    table->lock = MUTEX_NEW;
    table->refcount = 1;
    return table;
}

void fd_table_ref(fd_table_t* table) {
    __atomic_add_fetch(&table->refcount, 1, __ATOMIC_SEQ_CST);
}

void fd_table_put(fd_table_t* table) {
    if (__atomic_sub_fetch(&table->refcount, 1, __ATOMIC_SEQ_CST) == 0) {
        for (int i = 0; i < MAX_FDS; i++) {
            if (table->entries[i].file) {
                file_put(table->entries[i].file);
            }
        }
        slab_free(fd_table_cache, table);
    }
}

int fd_install(fd_table_t* table, file_t* file, int flags) {
    if (!file)
        return -EINVAL;

    mutex_lock(&table->lock);
    for (int i = 0; i < MAX_FDS; i++) {
        if (table->entries[i].file == nullptr) {
            table->entries[i].file = file;
            table->entries[i].flags = flags;
            mutex_unlock(&table->lock);
            return i;
        }
    }
    mutex_unlock(&table->lock);
    return -EMFILE;
}

file_t* fd_get(fd_table_t* table, int fd) {
    if (fd < 0 || fd >= MAX_FDS)
        return nullptr;

    mutex_lock(&table->lock);

    file_t* file = table->entries[fd].file;
    if (file)
        file_ref(file);

    mutex_unlock(&table->lock);
    return file;
}

int fd_close(fd_table_t* table, int fd) {
    if (fd < 0 || fd >= MAX_FDS)
        return -EBADF;

    mutex_lock(&table->lock);
    file_t* file = table->entries[fd].file;
    if (!file) {
        mutex_unlock(&table->lock);
        return -EBADF;
    }

    table->entries[fd].file = nullptr;
    table->entries[fd].flags = 0;
    mutex_unlock(&table->lock);

    file_put(file);
    return 0;
}

int fd_dup(fd_table_t* table, int oldfd) {
    if (oldfd < 0 || oldfd >= MAX_FDS)
        return -EBADF;

    mutex_lock(&table->lock);

    file_t* file = table->entries[oldfd].file;
    if (!file) {
        mutex_unlock(&table->lock);
        return -EBADF;
    }

    int newfd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (table->entries[i].file == nullptr) {
            newfd = i;
            break;
        }
    }

    if (newfd < 0) {
        mutex_unlock(&table->lock);
        return -EMFILE;
    }

    file_ref(file);
    table->entries[newfd].file = file;

    table->entries[newfd].flags = table->entries[oldfd].flags;

    mutex_unlock(&table->lock);
    return newfd;
}


void fd_init() {
    fd_table_cache = slab_create_cache("fdtable", sizeof(fd_table_t), 4 * PAGE_SIZE);
}
