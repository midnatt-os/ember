#include "fs/file.h"

#include <stddef.h>

#include "abi/fcntl.h"
#include "abi/seek_whence.h"
#include "abi/errno.h"
#include "common/lock/mutex.h"
#include "common/log.h"
#include "common/types.h"
#include "fs/vnode.h"
#include "lib/ref_count.h"
#include "memory/heap.h"



File* file_create(VNode* node, int flags) {
    File* f = kmalloc(sizeof(File));
    *f = (File) {
        .ref_count = 1,
        .lock = MUTEX_NEW,

        .node = node,
        .off = 0,

        .flags = flags,
        .append = (flags & O_APPEND) != 0,

        .ops = nullptr
    };

    vnode_ref(node);

    return f;
}

void file_close(File* f) {
    if (ref_count_dec(&f->ref_count) != 0)
        return

    vnode_unref(f->node);
    logln(LOG_DEBUG, "FILE", "file_close (free)");
}

ssize_t file_read(File* f, void* buf , size_t len) {
    mutex_acquire(&f->lock);
    ssize_t res = f->node->ops->read(f->node, buf, len, f->off);

    if (res <= 0) {
        mutex_release(&f->lock);
        return res;
    }

    f->off += res;
    mutex_release(&f->lock);

    return res;
}

ssize_t file_write(File* f, const void* buf , size_t len) {
    mutex_acquire(&f->lock);

    Attributes attr;
    int error = f->node->ops->get_attr(f->node, &attr);
    if (error < 0) {
        mutex_release(&f->lock);
        return error;
    }

    off_t start = f->append ? attr.size : f->off;

    ssize_t res = f->node->ops->write(f->node, buf, len, start);
    if (res < 0) {
        mutex_release(&f->lock);
        return res;
    }

    f->off = start + res;

    mutex_release(&f->lock);
    return res;
}

off_t file_seek(File* f, off_t offset, int whence) {
    mutex_acquire(&f->lock);

    off_t base;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = f->off;
            break;
        case SEEK_END: {
            Attributes attr;
            int err = f->node->ops->get_attr(f->node, &attr);
            if (err < 0) {
                mutex_release(&f->lock);
                return -err;
            }
            base = attr.size;
            break;
        }
        default:
            mutex_release(&f->lock);
            return -EINVAL;
    }

    off_t new_off = base + offset;
    if (new_off < 0) {
        mutex_release(&f->lock);
        return -EINVAL;
    }

    f->off = new_off;
    mutex_release(&f->lock);
    return new_off;
}
