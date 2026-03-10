#pragma once

#include "fs/vfs.h"

#include <stdint.h>

typedef struct tmpfs_node tmpfs_node_t;

struct tmpfs_node {
    uint64_t id;
    const char* name;
    vnode_t* vnode;
    tmpfs_node_t* parent;
    union {
        struct {
            list_t children;
        } dir;

        struct {
            void* base;
            size_t size;
        } file;

        struct {
            char* target;
            size_t len;
        } link;
    };
    list_node_t dir_node;
};

void tmpfs_init();
