#pragma once

#include "fs/vfs.h"
#include "lib/list.h"

typedef struct {
    ssize_t (*read)(void* buffer, size_t count, off_t offset);
    ssize_t (*write)(const void* buffer, size_t count, off_t offset);
} dev_ops_t;

typedef struct devfs_node devfs_node_t;

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
