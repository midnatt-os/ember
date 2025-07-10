#pragma once

#include "fs/vfs.h"
#include "fs/vnode.h"
#include <stdint.h>

typedef struct {
    ssize_t (*read)(void* ctx, void* buf, size_t len, off_t off);
    ssize_t (*write)(void* ctx, const void* buf, size_t len, off_t off);
    int (*ioctl)(void* ctx, uint64_t request, void* argp);
} DeviceOps;

extern MountOps devfs_ops;

void devfs_register(const char* parent_path, const char* name, VNodeType type, DeviceOps* dev_ops, void* dev_ctx);
