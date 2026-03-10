#pragma once

#include "fs/vfs.h"
#include "lib/list.h"

typedef struct devfs_node devfs_node_t;

typedef struct {
    int (*open)(devfs_node_t* node, uint32_t flags, vnode_t** out_vn);
    ssize_t (*read)(void* buffer, size_t count, off_t offset);
    ssize_t (*write)(const void* buffer, size_t count, off_t offset);
    int (*ioctl)(devfs_node_t* node, uint64_t req, uintptr_t u_arg);
    poll_mask_t (*poll)(devfs_node_t* node, poll_table_t* pt);
} dev_ops_t;

struct devfs_node {
    const char* name;
    vnode_t* vnode;
    devfs_node_t* parent;

    int major;
    int minor;

    union {
        struct {
            list_t children;
        } dir;
        struct {
            dev_ops_t* ops;
        } device;
    };
    list_node_t dir_node;
};

// TODO: driver registry (only take in maj. min.)
int devfs_make_node(const char* path, vnode_type_t type, int major, int minor, dev_ops_t* ops);

void devfs_init();
