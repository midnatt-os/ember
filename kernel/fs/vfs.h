#pragma once

#include "common/types.h"
#include "fs/vnode.h"
#include "lib/list.h"
#include <stddef.h>

#define MAX_NAME_LEN 255
#define MAX_COMPONENT_LEN MAX_NAME_LEN
#define MAX_PATH_LEN 4096

typedef struct Mount Mount;
typedef struct VNode VNode;

typedef struct {
    int (*root)(Mount* mount, VNode** root_node);
    int (*mount)(Mount* mount);
} MountOps;

struct Mount {
    char fs_type[16];
    MountOps* ops;
    VNode* covered_node;
    void* data;

    ListNode mounts_node;
};

int vfs_mount(const char* path, const char* fs_type, MountOps* ops);

int vfs_split_path(const char *path, char* parent_buf, size_t parent_size, char* name_buf, size_t name_size);

int vfs_root(VNode** root);
int vfs_lookup(VNode* start, const char* path, VNode** result);
int vfs_create_file(VNode* start, const char* parent_path, const char* name, VNode** new);
int vfs_create_dir(VNode* start, const char* parent_path, const char* name, VNode** new);
ssize_t vfs_read(VNode* start, const char* path, void* buf, size_t len, off_t off);
ssize_t vfs_write(VNode* start, const char* path, const void* buf, size_t len, off_t off);
int vfs_get_attr(VNode* start, const char* path, Attributes* attr);
