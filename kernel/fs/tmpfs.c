#include "fs/tmpfs.h"

#include <stdint.h>

#include "common/assert.h"
#include "lib/math.h"
#include "lib/mem.h"
#include "lib/string.h"
#include "memory/heap.h"

#define TMPFS_CTX(VFS) ((TmpfsInfo*) (VFS)->fs_ctx)
#define TMPFS_NODE(VNODE) ((TmpfsNode*) (VNODE)->node_ctx)

typedef struct {
    uint64_t id;
    char* name;
    VNode* vnode;

    union {
        struct { List children; } dir;
        struct { void* base; size_t length; } file;
    };

    ListNode list_node;
} TmpfsNode;

typedef struct {
    TmpfsNode* root;
    uint64_t id_counter;
} TmpfsInfo;



static VNodeOps tmpfs_node_ops;

static TmpfsNode* create_tmpfs_node(Vfs* vfs, char* name, bool is_dir) {
    VNode* vnode = kmalloc(sizeof(VNode));

    *vnode = (VNode) {
        .vfs = vfs,
        .ops = &tmpfs_node_ops,
        .type = is_dir ? VNODE_DIR : VNODE_FILE,
    };

    TmpfsNode* node = kmalloc(sizeof(TmpfsNode));

    *node = (TmpfsNode) {
        .id = TMPFS_CTX(vfs)->id_counter++,
        .name = name,
    };

    if (is_dir) {
        node->dir.children = LIST_NEW;
    } else {
        node->file.base = nullptr;
        node->file.length = 0;
    }

    node->vnode = vnode;
    vnode->node_ctx = node;

    return node;
}

static TmpfsNode* find_in_dir(TmpfsNode* dir_node, char* name) {
    ASSERT(dir_node->vnode->type == VNODE_DIR);

    LIST_FOREACH(dir_node->dir.children, n) {
        TmpfsNode* node = LIST_ELEMENT_OF(n, TmpfsNode, list_node);

        if (streq(node->name, name))
            return node;
    }

    return nullptr;
}

/* TMPFS NODE ops  */

static VfsResult tmpfs_node_lookup(VNode* node, char* name, VNode** found_node) {
    if (node->type != VNODE_DIR)
        return VFS_RES_NOT_DIR;

    TmpfsNode* tmpfs_node = find_in_dir((TmpfsNode*) node->node_ctx, name);
    if (tmpfs_node == nullptr)
        return VFS_RES_NOT_FOUND;

    *found_node = tmpfs_node->vnode;

    return VFS_RES_OK;
}

/* TMPFS ops */

static VfsResult tmpfs_mount(Vfs* vfs) {
    TmpfsInfo* info = kmalloc(sizeof(TmpfsInfo));

    vfs->fs_ctx = info;
    info->root = create_tmpfs_node(vfs, "root", true); // TODO: should the root node really be named root?

    return VFS_RES_OK;
}

static VfsResult tmpfs_root(Vfs* vfs, VNode** node) {
    *node = TMPFS_CTX(vfs)->root->vnode;
    return VFS_RES_OK;
}

static VfsResult tmpfs_read_dir(VNode* node, size_t* offset, DirEntry* dir_entry) {
    TmpfsNode* tmp_node = TMPFS_NODE(node);
    size_t i = 0;

    LIST_FOREACH(tmp_node->dir.children, n) {
        TmpfsNode* child = LIST_ELEMENT_OF(n, TmpfsNode, list_node);

        if (i++ < *offset)
            continue;

        dir_entry->name = child->name;
        dir_entry->type = child->vnode->type;

        *offset = i;
        return VFS_RES_OK;
    }

    return VFS_RES_END;
}

VfsResult tmpfs_create_file(VNode* parent, char* name, VNode** new_node) {
    if (parent->type != VNODE_DIR)
        return VFS_RES_NOT_DIR;

    TmpfsNode* dir = TMPFS_NODE(parent);

    TmpfsNode* file_node = create_tmpfs_node(parent->vfs, name, false);
    list_append(&dir->dir.children, &file_node->list_node);
    *new_node = file_node->vnode;

    return VFS_RES_OK;
}

VfsResult tmpfs_create_dir(VNode* parent, char* name, VNode** new_node) {
    if (parent->type != VNODE_DIR)
        return VFS_RES_NOT_DIR;

    TmpfsNode* dir = TMPFS_NODE(parent);

    TmpfsNode* file_node = create_tmpfs_node(parent->vfs, name, true);
    list_append(&dir->dir.children, &file_node->list_node);
    *new_node = file_node->vnode;

    return VFS_RES_OK;
}

VfsResult tmpfs_write(VNode* node, void* buf, size_t off, size_t len, size_t* written) {
    TmpfsNode* n = TMPFS_NODE(node);

    *written = 0;

    if (node->type != VNODE_FILE)
        return VFS_RES_NOT_FILE;

    if (off + len > n->file.length) {
        n->file.base = krealloc(n->file.base, n->file.length, off + len);
        n->file.length = off + len;
    }

    memcpy(n->file.base + off, buf, len);
    *written = len;

    return VFS_RES_OK;
}

// TODO: Combine read and write?
VfsResult tmpfs_read(VNode* node, void* buf, size_t off, size_t len, size_t* read) {
    TmpfsNode* n = TMPFS_NODE(node);

    *read = 0;

    if (node->type != VNODE_FILE)
        return VFS_RES_NOT_FILE;

    size_t available = n->file.length > off ? n->file.length - off : 0;
    size_t to_read = available < len ? available : len;

    memcpy(buf, n->file.base + off, to_read);
    *read = to_read;

    return VFS_RES_OK;
}

static VNodeOps tmpfs_node_ops = {
    .lookup      = tmpfs_node_lookup,
    .read_dir    = tmpfs_read_dir,
    .create_file = tmpfs_create_file,
    .create_dir  = tmpfs_create_dir,
    .write       = tmpfs_write,
    .read        = tmpfs_read,
};

VfsOps tmpfs_ops = { .mount = tmpfs_mount, .root = tmpfs_root };
