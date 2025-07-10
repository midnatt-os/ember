#pragma once

#include "common/lock/mutex.h"
#include "common/types.h"
#include "fs/vnode.h"
#include "lib/ref_count.h"
#include <stddef.h>

typedef struct {
    ref_t ref_count;
    Mutex lock;

    VNode* node;
    off_t off;

    int flags;
    bool append;

    bool is_tty;

    struct FileOps {}* ops; // use if node == nullptr, otherwise use node->ops.
} File;

File* file_create(VNode* node, int flags);
void file_close(File* f);
ssize_t file_read(File* f, void* buf , size_t len);
ssize_t file_write(File* f, const void* buf , size_t len);
off_t file_seek(File* f, off_t offset, int whence);
ssize_t file_ioctl(File* f, uint64_t request, void* argp);
int file_stat(File *f, Attributes* attr);
