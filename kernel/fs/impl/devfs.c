#include "fs/impl/devfs.h"

#include "lib/container.h"
#include "lib/mem.h"
#include "lib/string.h"
#include "mem/heap.h"

#include <common/errno.h>

typedef struct {
    devfs_node_t* root;
} devfs_info_t;

static vnode_ops_t devfs_vnode_ops;
static mount_t* devfs_mount_instance = nullptr; // TODO: ???

#define INFO(MOUNT) ((devfs_info_t*) (MOUNT)->private)

static devfs_node_t* new_node(devfs_node_t* parent, mount_t* mount, vnode_type_t type, const char* name) {
    devfs_node_t* node = heap_alloc(sizeof(devfs_node_t));
    char* name_copy = heap_alloc(strlen(name) + 1);
    strcpy(name_copy, name);

    *node = (devfs_node_t) {
        .name = name_copy,
        .parent = parent,
    };

    if (type == V_DIR) {
        node->dir.children = LIST_NEW;
    }

    vnode_t* vnode = heap_alloc(sizeof(vnode_t));
    *vnode = (vnode_t) {
        .type = type,
        .ops = &devfs_vnode_ops,
        .mount = mount,
        .private = node,
    };

    node->vnode = vnode;
    if (parent) {
        list_append(&parent->dir.children, &node->dir_node);
    }

    return node;
}

static int devfs_lookup(vnode_t* dir, const char* name, vnode_t** result) {
    devfs_node_t* d = (devfs_node_t*) dir->private;

    if (strcmp(name, "..") == 0) {
        *result = d->parent ? d->parent->vnode : dir;
        return 0;
    }

    LIST_FOREACH(d->dir.children, curr) {
        devfs_node_t* child = CONTAINER_OF(curr, devfs_node_t, dir_node);
        if (strcmp(child->name, name) == 0) {
            *result = child->vnode;
            return 0;
        }
    }
    return -ENOENT;
}

static ssize_t devfs_read(vnode_t* file, void* buffer, size_t count, off_t offset) {
    devfs_node_t* node = (devfs_node_t*) file->private;
    if (file->type == V_DIR)
        return -EISDIR;
    if (!node->device.ops || !node->device.ops->read)
        return -ENOSYS;

    return node->device.ops->read(buffer, count, offset);
}

static ssize_t devfs_write(vnode_t* file, const void* buffer, size_t count, off_t offset) {
    devfs_node_t* node = (devfs_node_t*) file->private;

    if (file->type == V_DIR)
        return -EISDIR;

    if (!node->device.ops || !node->device.ops->write)
        return -ENOSYS;

    return node->device.ops->write(buffer, count, offset);
}

static int devfs_getattr(vnode_t* node, stat_t* out) {
    (void) node;
    (void) out;
    return -ENOSYS;
}

static vnode_ops_t devfs_vnode_ops = {
    .lookup = devfs_lookup,
    .read = devfs_read,
    .write = devfs_write,
    .getattr = devfs_getattr,
};

static int devfs_mount(mount_t* m) {
    devfs_info_t* info = heap_alloc(sizeof(devfs_info_t));
    m->private = info;

    info->root = new_node(nullptr, m, V_DIR, "dev");
    m->root = info->root->vnode;

    devfs_mount_instance = m;
    return 0;
}

static int devfs_root(mount_t* m, vnode_t** root_vnode) {
    *root_vnode = INFO(m)->root->vnode;
    return 0;
}

static mount_ops_t devfs_ops = {
    .mount = devfs_mount,
    .root = devfs_root,
};

static vfs_fstype_t devfs_fstype = {
    .name = "devfs",
    .ops = &devfs_ops,
};

int devfs_make_node(const char* path, vnode_type_t type, int major, int minor, dev_ops_t* ops) {
    if (!devfs_mount_instance)
        return -ENODEV;

    devfs_node_t* curr = (devfs_node_t*) devfs_mount_instance->root->private;

    const char* p = path;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;

        const char* start = p;
        while (*p && *p != '/')
            p++;
        size_t len = p - start;

        char* component = heap_alloc(len + 1);
        memcpy(component, start, len);
        component[len] = '\0';

        vnode_t* next_vn = nullptr;
        int err = devfs_lookup(curr->vnode, component, &next_vn);

        if (err == -ENOENT) {
            vnode_type_t node_type = (*p == '\0') ? type : V_DIR;
            devfs_node_t* next_node = new_node(curr, devfs_mount_instance, node_type, component);

            if (*p == '\0') {
                next_node->major = major;
                next_node->minor = minor;
                next_node->device.ops = ops;
            }
            curr = next_node;
        } else {
            curr = (devfs_node_t*) next_vn->private;
        }

        heap_free(component, len + 1);
    }
    return 0;
}

void devfs_init() {
    vfs_register(&devfs_fstype);
}
