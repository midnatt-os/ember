#pragma once

#include "lib/hashmap.h"

#include <stddef.h>
#include <stdint.h>

#pragma once

#include "lib/list.h"

typedef struct {
    size_t size;
} stat_t;

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
    V_CHR,
    V_BLK,
    V_LNK,
} vnode_type_t;


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
    int (*symlink)(vnode_t* dir, const char* name, const char* target);
    ssize_t (*readlink)(vnode_t* link, char* buf, size_t buf_len);
    ssize_t (*read)(vnode_t* file, void* buffer, size_t count, off_t offset);
    ssize_t (*write)(vnode_t* file, const void* buffer, size_t count, off_t offset);
    int (*getattr)(vnode_t* node, stat_t* st);
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
ssize_t vfs_write(vnode_t* vnode, const void* buffer, size_t count, off_t offset);
int vfs_getattr(vnode_t* node, stat_t* st);

int vfs_symlink(path_t linkpath, const char* target);
ssize_t vfs_readlink(vnode_t* link, char* buf, size_t buflen);

int vfs_register(vfs_fstype_t* fs_type);
int vfs_mount(const char* fstype_name, const char* target_path);

void vfs_init();
