#pragma once

#include "lib/list.h"

typedef struct VfsOps VfsOps;
typedef struct VNode VNode;

typedef enum {
    VFS_RES_OK,
    VFS_RES_ERR,
    VFS_RES_NOT_FOUND,
    VFS_RES_NOT_DIR,
    VFS_RES_END,
    VFS_RES_NOT_FILE,
} VfsResult;

typedef struct {
    char* mount_point;
    VfsOps* ops;
    void* fs_ctx;
    ListNode list_node;
} Vfs;

struct VfsOps {
    VfsResult (*mount)(Vfs* vfs);
    VfsResult (*root)(Vfs* vfs, VNode** node);
};

typedef enum { VNODE_FILE, VNODE_DIR } VNodeType;

typedef struct VNodeOps VNodeOps;

struct VNode {
    Vfs* vfs;
    VNodeOps* ops;
    VNodeType type;
    void* node_ctx;
};

typedef struct {
    char* name;
    VNodeType type;
} DirEntry;

struct VNodeOps {
    VfsResult (*lookup)(VNode* node, char* name, VNode** found_node);
    VfsResult (*read_dir)(VNode* node, size_t* offset, DirEntry* dir_entry);
    VfsResult (*create_file)(VNode* parent, char* name, VNode** new_node);
    VfsResult (*create_dir)(VNode* parent, char* name, VNode** new_node);
    VfsResult (*write)(VNode* node, void* buf, size_t off, size_t len, size_t* written);
    VfsResult (*read)(VNode* node, void* buf, size_t off, size_t len, size_t* read);
};

VfsResult vfs_lookup(char* path, VNode** found_node);
VfsResult vfs_read_dir(char* path, size_t* offset, DirEntry* dir_entry);
VfsResult vfs_create_file(char* path, VNode** new_node);
VfsResult vfs_create_dir(char* path, VNode** new_node);
VfsResult vfs_write(char* path, void* buf, size_t off, size_t len, size_t* written);
VfsResult vfs_read(char* path, void* buf, size_t off, size_t len, size_t* read);

VfsResult vfs_mount(char* path, VfsOps* ops);
