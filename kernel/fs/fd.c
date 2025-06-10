#include "fd.h"

#include <stddef.h>

#include "abi/errno.h"
#include "common/lock/mutex.h"
#include "fs/file.h"
#include "lib/ref_count.h"




static inline bool valid_fd(int fd) {
    return fd >= 0 && fd < MAX_FDS;
}

void fd_init(FDTable* table) {
    table->lock = MUTEX_NEW;
    for (size_t i = 0; i < MAX_FDS; i++)
        table->fds[i] = nullptr;
}

void fd_clone_table(FDTable* to_clone, FDTable* copy) {
    mutex_acquire(&to_clone->lock);

    copy->lock = MUTEX_NEW;

    for (size_t i = 0; i < MAX_FDS; i++) {
        File* f = to_clone->fds[i];
        copy->fds[i] = f;

        if (f != nullptr)
            ref_count_inc(&f->ref_count);
    }

    mutex_release(&to_clone->lock);
}

int fd_alloc(FDTable* table, File* file) {
    mutex_acquire(&table->lock);
    for (size_t i = 0; i < MAX_FDS; i++) {
        if (table->fds[i] == nullptr) {
            table->fds[i] = file;
            mutex_release(&table->lock);
            return i;
        }
    }

    mutex_release(&table->lock);
    return -EMFILE;
}

int fd_get(FDTable* table, int fd, File** file) {
    mutex_acquire(&table->lock);
    if (!valid_fd(fd)|| table->fds[fd] == nullptr) {
        mutex_release(&table->lock);
        return -EBADF;
    }


    *file = table->fds[fd];

    mutex_release(&table->lock);
    return 0;
}

int fd_close(FDTable* table, int fd) {
    mutex_acquire(&table->lock);
    if (!valid_fd(fd) || table->fds[fd] == nullptr) {
        mutex_release(&table->lock);
        return -EBADF;
    }

    File* f = table->fds[fd];
    table->fds[fd] = nullptr;

    file_close(f);

    mutex_release(&table->lock);
    return 0;
}
