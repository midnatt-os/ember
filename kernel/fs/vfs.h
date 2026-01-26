#pragma once

#include "lib/hashmap.h"

#include <stdint.h>

#pragma once

#include "lib/list.h"

typedef struct vnode vnode_t;
typedef struct mount_ops mount_ops_t;

typedef struct {
    vnode_t* root;
    vnode_t* covered_vnode;
    const mount_ops_t* ops;
    void* private;

    list_node_t mounts_node;
} mount_t;

typedef enum {
    V_REG,
    V_DIR,
} vnode_type_t; // V_CHR, V_BLK


typedef struct vnode_ops vnode_ops_t;

struct vnode {
    vnode_type_t type;
    vnode_ops_t* ops;
    mount_t* mount;
    mount_t* covering_mount;

    void* private;
};

typedef int64_t ssize_t;
typedef int64_t off_t;

struct vnode_ops {
    int (*lookup)(vnode_t* dir, const char* name, vnode_t** result);
    int (*create)(vnode_t* dir, const char* name);
    int (*mkdir)(vnode_t* dir, const char* name);
    ssize_t (*read)(vnode_t* file, void* buffer, size_t count, off_t offset);
};

struct mount_ops {
    int (*mount)(mount_t* m);
    int (*unmount)(mount_t* m);
    int (*root)(mount_t* m, vnode_t** root);
};

typedef struct {
    const char* name;
    const mount_ops_t* ops;
    hashmap_node_t node;
} vfs_fstype_t;

typedef struct {
    vnode_t* base;
    const char* path;
} path_t;

#define ABS_PATH(p) ((path_t) { .base = nullptr, .path = (p) })
#define REL_PATH(b, p) ((path_t) { .base = (b), .path = (p) })

// Returns the current root vnode (may be nullptr if no root is mounted yet).
vnode_t* vfs_get_root();

int vfs_lookup(path_t path, vnode_t** result);
int vfs_create(path_t path);
int vfs_mkdir(path_t path);
ssize_t vfs_read(vnode_t* file, void* buffer, size_t count, off_t offset);

int vfs_register(vfs_fstype_t* fs_type);
int vfs_mount(const char* fstype_name, const char* target_path);

void vfs_init();
