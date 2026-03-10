#include "fs/file.h"

#include "common/assert.h"
#include "common/errno.h"
#include "common/lock/mutex.h"
#include "fs/vfs.h"
#include "mem/page.h"
#include "mem/slab.h"

#define O_ACCMODE 00000003


object_cache_t* file_cache = nullptr;

void file_free(file_t* file) {
    ASSERT(file->refcount == 0);
    ASSERT(file->mutex.state == MUTEX_STATE_UNLOCKED);

    // TODO: PUT vnode

    *file = (file_t) {};

    slab_free(file_cache, file);
}

void file_ref(file_t* file) {
    __atomic_add_fetch(&file->refcount, 1, __ATOMIC_SEQ_CST);
}

void file_put(file_t* file) {
    if (__atomic_sub_fetch(&file->refcount, 1, __ATOMIC_SEQ_CST) == 0)
        file_free(file);
}

file_t* file_alloc(vnode_t* vnode, int flags) {
    file_t* file = slab_alloc(file_cache);

    file->vnode = vnode;
    file->refcount = 1;
    file->offset = 0;
    file->flags = flags;
    file->mutex = MUTEX_NEW;

    // TODO: GET vnode

    return file;
}

ssize_t file_read(file_t* file, void* buf, size_t count) {
    mutex_lock(&file->mutex);

    int mode = file->flags & O_ACCMODE;
    if (mode != O_RDONLY && mode != O_RDWR) {
        mutex_unlock(&file->mutex);
        return -EBADF;
    }

    ssize_t result = vfs_read(file->vnode, buf, count, file->offset);

    if (result > 0)
        file->offset += result;

    mutex_unlock(&file->mutex);
    return result;
}

ssize_t file_write(file_t* file, const void* buf, size_t count) {
    if (!file || (!buf && count != 0))
        return -EINVAL;

    mutex_lock(&file->mutex);

    int mode = file->flags & O_ACCMODE;
    if (mode != O_WRONLY && mode != O_RDWR) {
        mutex_unlock(&file->mutex);
        return -EBADF;
    }

    if (count == 0) {
        mutex_unlock(&file->mutex);
        return 0;
    }

    // Handle O_APPEND: always write at end
    if (file->flags & O_APPEND) {
        stat_t st;
        int err = vfs_getattr(file->vnode, &st);
        if (err != 0) {
            mutex_unlock(&file->mutex);
            return err;
        }
        file->offset = st.size;
    }

    ssize_t result = vfs_write(file->vnode, buf, count, (off_t) file->offset);

    if (result > 0)
        file->offset += (uintmax_t) result;

    mutex_unlock(&file->mutex);
    return result;
}

off_t file_seek(file_t* file, off_t offset, int whence) {
    mutex_lock(&file->mutex);

    uintmax_t new_offset = file->offset;
    stat_t st;

    switch (whence) {
        case SEEK_SET: new_offset = offset; break;
        case SEEK_CUR: new_offset = file->offset + offset; break;
        case SEEK_END: {
            if (vfs_getattr(file->vnode, &st) != 0) {
                mutex_unlock(&file->mutex);
                return -EIO;
            }
            new_offset = st.size + offset;
            break;
        }
        default: {
            mutex_unlock(&file->mutex);
            return -EINVAL;
        }
    }

    if ((intmax_t) new_offset < 0) {
        mutex_unlock(&file->mutex);
        return -EINVAL;
    }

    file->offset = new_offset;

    mutex_unlock(&file->mutex);

    return new_offset;
}

poll_mask_t file_poll(file_t* file, poll_table_t* pt) {
    if (!file || !file->vnode || !file->vnode->ops)
        return POLLNVAL;

    if (file->vnode->ops->poll)
        return file->vnode->ops->poll(file->vnode, pt);

    return POLLIN | POLLOUT;
}

void file_init() {
    file_cache = slab_create_cache("file", sizeof(file_t), PAGE_SIZE);
}
