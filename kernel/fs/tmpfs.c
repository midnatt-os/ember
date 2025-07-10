#include "fs/tmpfs.h"

#include <stddef.h>
#include <stdint.h>

#include "common/assert.h"
#include "common/lock/mutex.h"
#include "common/log.h"
#include "fs/vfs.h"
#include "lib/container.h"
#include "lib/list.h"
#include "lib/math.h"
#include "lib/mem.h"
#include "lib/string.h"
#include "memory/heap.h"
#include "fs/vnode.h"
#include "abi/errno.h"

#define TMPFS_CTX(VFS) ((TmpfsInfo*) (VFS)->data)
#define TMPFS_NODE(VNODE) ((TmpfsNode*) (VNODE)->data)

typedef struct TmpfsNode {
    uint64_t id;
    char name[MAX_NAME_LEN+1];
    VNode* vnode;

    struct TmpfsNode* parent;

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

static TmpfsNode* create_tmpfs_node(Mount* mount, const char* name, bool is_dir) {
    VNode* vnode = kmalloc(sizeof(VNode));

    *vnode = (VNode) {
        .mount = mount,
        .ops = &tmpfs_node_ops,
        .type = is_dir ? VNODE_TYPE_DIR : VNODE_TYPE_FILE,
        .lock = MUTEX_NEW,
        .ref_count = 0
    };

    TmpfsNode* node = kmalloc(sizeof(TmpfsNode));

    *node = (TmpfsNode) {
        .id = TMPFS_CTX(mount)->id_counter++,
    };

    strncpy(node->name, name, strlen(name)+1);

    if (is_dir) {
        node->dir.children = LIST_NEW;
    } else {
        node->file.base = nullptr;
        node->file.length = 0;
    }

    node->vnode = vnode;
    vnode->data = node;

    return node;
}

static TmpfsNode* find_in_dir(TmpfsNode* dir_node, const char* name) {
    ASSERT(dir_node->vnode->type == VNODE_TYPE_DIR);

    if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
        return dir_node->parent;

    LIST_FOREACH(dir_node->dir.children, n) {
        TmpfsNode* node = CONTAINER_OF(n, TmpfsNode, list_node);

        if (streq(node->name, name))
            return node;
    }

    return nullptr;
}

/* TMPFS NODE ops  */

static int tmpfs_lookup(VNode* node, const char* name, VNode** found) {
    if (node->type != VNODE_TYPE_DIR)
        return -ENOTDIR;

    TmpfsNode* match = find_in_dir(TMPFS_NODE(node), name);
    if (match == nullptr)
        return -ENOENT;

    vnode_ref(match->vnode);
    *found = match->vnode;

    return 0;
}

/* TMPFS ops */

static int tmpfs_mount(Mount* mount) {
    TmpfsInfo* info = kmalloc(sizeof(TmpfsInfo));

    mount->data = info;
    info->root = create_tmpfs_node(mount, "root", true); // TODO: should the root node really be named root?
    info->root->vnode->is_root = true;

    return 0;
}

static int tmpfs_root(Mount* mount, VNode** node) {
    *node = TMPFS_CTX(mount)->root->vnode;
    vnode_ref(*node);
    return 0;
}

void tmpfs_free([[maybe_unused]] VNode* node) {
    //logln(LOG_DEBUG, "TMPFS", "tmpfs_free(%#p (%s))", node, TMPFS_NODE(node)->name);
}

/*
static int tmpfs_read_dir(VNode* node, size_t* offset, DirEntry* dir_entry) {
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
}*/

int tmpfs_create_file(VNode* parent, const char* name, VNode** new) {
    if (parent->type != VNODE_TYPE_DIR)
        return -ENOTDIR;

    TmpfsNode* dir = TMPFS_NODE(parent);

    TmpfsNode* child = create_tmpfs_node(parent->mount, name, false);

    child->parent = dir;
    list_append(&dir->dir.children, &child->list_node);

    vnode_ref(child->vnode);
    *new = child->vnode;

    return 0;
}

int tmpfs_create_dir(VNode* parent, const char* name, VNode** new) {
    if (parent->type != VNODE_TYPE_DIR)
        return -ENOTDIR;

    TmpfsNode* dir = TMPFS_NODE(parent);

    TmpfsNode* child = create_tmpfs_node(parent->mount, name, true);

    child->parent = dir;
    list_append(&dir->dir.children, &child->list_node);

    vnode_ref(child->vnode);
    *new = child->vnode;

    return 0;
}

ssize_t tmpfs_read(VNode* node, void* buf, size_t len, off_t off) {
    TmpfsNode* n = TMPFS_NODE(node);

    if (node->type == VNODE_TYPE_DIR)
        return -EISDIR;

    if ((size_t) off >= n->file.length)
        return 0;

    size_t available = n->file.length - (size_t) off;
    size_t to_read = MATH_MIN(len, available);

    memcpy(buf, n->file.base + off, to_read);
    return to_read;
}

ssize_t tmpfs_write(VNode* node, const void* buf, size_t len, off_t off) {
    TmpfsNode *n = TMPFS_NODE(node);

    if (node->type == VNODE_TYPE_DIR)
        return -EISDIR;

    size_t new_len = (size_t) off + len;
    if (new_len > n->file.length) {
        void *new_base = krealloc(n->file.base, n->file.length, new_len);
        n->file.base = new_base;
        n->file.length = new_len;
    }

    memcpy(n->file.base + off, buf, len);
    return len;
}

int tmpfs_get_attr(VNode* node, Attributes* attr) {
    TmpfsNode* n = TMPFS_NODE(node);

    *attr = (Attributes) {
        .size = n->file.length,
    };

    return 0;
}

int tmpfs_ioctl([[maybe_unused]] VNode* node, [[maybe_unused]] uint64_t request, [[maybe_unused]] void* argp) {
    return -ENOTTY;
}

static VNodeOps tmpfs_node_ops = {
    .lookup      = tmpfs_lookup,
    .free        = tmpfs_free,
    .create_file = tmpfs_create_file,
    .create_dir  = tmpfs_create_dir,
    .read        = tmpfs_read,
    .write       = tmpfs_write,
    .get_attr    = tmpfs_get_attr,
    .ioctl       = tmpfs_ioctl,
    //.read_dir    = tmpfs_read_dir,
};

MountOps tmpfs_ops = { .mount = tmpfs_mount, .root = tmpfs_root };
