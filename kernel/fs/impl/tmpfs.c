#include "fs/impl/tmpfs.h"

#include "common/log.h"
#include "fs/vfs.h"
#include "lib/container.h"
#include "lib/list.h"
#include "mem/heap.h"

#include <common/errno.h>
#include <lib/mem.h>
#include <lib/string.h>
#include <stddef.h>
#include <stdint.h>

#define INFO(MOUNT) ((tmpfs_info_t*) (MOUNT)->private)

typedef struct {
    tmpfs_node_t* root;
    uint64_t id_counter;
} tmpfs_info_t;

static vnode_ops_t tmpfs_vnode_ops;

static tmpfs_node_t* new_node(tmpfs_node_t* parent, mount_t* mount, vnode_type_t type, const char* name) {
    tmpfs_node_t* node = heap_alloc(sizeof(tmpfs_node_t));
    char* name_copy = heap_alloc(strlen(name) + 1);
    strcpy(name_copy, name);

    *node = (tmpfs_node_t) {
        .name = name_copy,
        .id = INFO(mount)->id_counter++,
        .parent = parent,
    };

    if (type == V_DIR) {
        node->dir.children = LIST_NEW;
    } else if (type == V_REG) {
        node->file.base = nullptr;
        node->file.size = 0;
    } else if (type == V_LNK) {
        node->link.target = nullptr;
        node->link.len = 0;
    }

    vnode_t* vnode = heap_alloc(sizeof(vnode_t));
    *vnode = (vnode_t) {
        .type = type,
        .ops = &tmpfs_vnode_ops,
        .mount = mount,
        .private = node,
    };

    node->vnode = vnode;

    if (parent) {
        list_append(&parent->dir.children, &node->dir_node);
    }

    return node;
}

static int tmpfs_open(vnode_t* vn, uint32_t oflags, vnode_t** out_vn) {
    (void) oflags;
    *out_vn = vn;
    return 0;
}

static int tmpfs_lookup(vnode_t* dir, const char* name, vnode_t** result) {
    tmpfs_node_t* d = (tmpfs_node_t*) dir->private;

    if (strcmp(name, "..") == 0) {
        tmpfs_node_t* parent = d->parent;
        *result = parent ? parent->vnode : dir;
        return 0;
    }

    LIST_FOREACH(d->dir.children, curr) {
        tmpfs_node_t* child = CONTAINER_OF(curr, tmpfs_node_t, dir_node);

        if (strcmp(child->name, name) == 0) {
            *result = child->vnode;
            return 0;
        }
    }

    return -ENOENT;
}

static int tmpfs_create(vnode_t* dir, const char* name) {
    vnode_t* result = nullptr;
    if (tmpfs_lookup(dir, name, &result) == 0) {
        return -EEXIST;
    }

    // 2. Get the parent tmpfs node from the generic vnode
    tmpfs_node_t* parent_node = (tmpfs_node_t*) dir->private;

    // 3. Create the new file node
    // Passing 'false' for is_dir makes it a regular file (V_REG).
    // Your new_node function handles the allocation, string copying, and list linking.
    new_node(parent_node, dir->mount, V_REG, name);

    return 0;
}

static int tmpfs_mkdir(vnode_t* dir, const char* name) {
    vnode_t* result = nullptr;

    // Check if a file/dir with this name already exists
    if (tmpfs_lookup(dir, name, &result) == 0) {
        return -EEXIST;
    }

    // Get the parent tmpfs node
    tmpfs_node_t* parent_node = (tmpfs_node_t*) dir->private;

    // Create the new directory node (is_dir = true)
    new_node(parent_node, dir->mount, V_DIR, name);

    return 0;
}

static ssize_t tmpfs_read(vnode_t* file, void* buffer, size_t count, off_t offset) {
    tmpfs_node_t* node = (tmpfs_node_t*) file->private;

    if (offset < 0) {
        return -EINVAL;
    }

    // 1. Check for End-Of-File (EOF)
    if ((size_t) offset >= node->file.size) {
        return 0; // 0 bytes read indicates EOF
    }

    // 2. Cap the read count if it exceeds the file size
    size_t bytes_to_read = count;
    if (offset + count > node->file.size) {
        bytes_to_read = node->file.size - offset;
    }

    // 3. Safety check: ensure file actually has allocated memory
    if (!node->file.base && bytes_to_read > 0) {
        return 0;
    }

    // 4. Perform the read
    memcpy(buffer, (uint8_t*) node->file.base + offset, bytes_to_read);
    return bytes_to_read;
}

static int tmpfs_getattr(vnode_t* node, stat_t* out) {
    tmpfs_node_t* n = (tmpfs_node_t*) node->private;
    memset(out, 0, sizeof(stat_t));

    if (node->type == V_DIR) {
        out->size = 4096;
    } else if (node->type == V_LNK) {
        out->size = n->link.len;
    } else {
        out->size = n->file.size;
    }

    return 0;
}


static int tmpfs_symlink(vnode_t* dir, const char* name, const char* target) {
    if (!dir || !name || !target)
        return -EINVAL;

    if (dir->type != V_DIR)
        return -ENOTDIR;

    // Disallow empty name
    if (name[0] == '\0')
        return -EINVAL;

    // Check if already exists
    vnode_t* existing = nullptr;
    if (tmpfs_lookup(dir, name, &existing) == 0)
        return -EEXIST;

    tmpfs_node_t* parent = (tmpfs_node_t*) dir->private;
    tmpfs_node_t* n = new_node(parent, dir->mount, V_LNK, name);

    size_t len = strlen(target);
    char* t = heap_alloc(len + 1);
    if (!t)
        return -ENOMEM;

    memcpy(t, target, len + 1);

    n->link.target = t;
    n->link.len = len;

    return 0;
}

static ssize_t tmpfs_readlink(vnode_t* link, char* buf, size_t buflen) {
    if (!link || !buf)
        return -EINVAL;

    if (link->type != V_LNK)
        return -EINVAL;

    tmpfs_node_t* n = (tmpfs_node_t*) link->private;
    if (!n->link.target)
        return 0;

    size_t ncopy = n->link.len;
    if (ncopy > buflen)
        ncopy = buflen;

    if (ncopy)
        memcpy(buf, n->link.target, ncopy);

    // Not NUL-terminated by contract
    return (ssize_t) ncopy;
}


static int tmpfs_unmount(mount_t* _) {
    logln(LOG_WARN, "TMPFS", "unmount stubbed");
    return -1;
}

static int tmpfs_root(mount_t* mount, vnode_t** root_vnode) {
    *root_vnode = INFO(mount)->root->vnode;
    return 0;
}

static int tmpfs_mount(mount_t* m) {
    tmpfs_info_t* info = heap_alloc(sizeof(tmpfs_info_t));
    m->private = info;

    info->id_counter = 0;
    info->root = new_node(nullptr, m, V_DIR, "root");

    m->root = info->root->vnode;

    return 0;
}

static vnode_ops_t tmpfs_vnode_ops = {
    .open = tmpfs_open,
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir,
    .read = tmpfs_read,
    .getattr = tmpfs_getattr,
    .symlink = tmpfs_symlink,
    .readlink = tmpfs_readlink,
};

static mount_ops_t tmpfs_ops = {
    .mount = tmpfs_mount,
    .unmount = tmpfs_unmount,
    .root = tmpfs_root,
};

static vfs_fstype_t tmpfs_fstype = {
    .name = "tmpfs",
    .ops = &tmpfs_ops,
};

void tmpfs_init() {
    vfs_register(&tmpfs_fstype);
}
