#include "fs/vfs.h"

#include "abi/errno.h"
#include "common/assert.h"
#include "common/types.h"
#include "fs/vnode.h"
#include "lib/container.h"
#include "lib/list.h"
#include "memory/heap.h"
#include "lib/mem.h"
#include "lib/string.h"
#include <stddef.h>
#include <stdint.h>


List mounts = LIST_NEW;

int vfs_mount(const char* path, const char* fs_type, MountOps* ops) {
    VNode* node = nullptr;

    if (list_is_empty(&mounts)) {
        ASSERT(streq(path, "/"));
    } else {
        int res = vfs_lookup(nullptr, path, &node);
        if (res < 0)
            return res;

        if (node->type != VNODE_TYPE_DIR) {
            vnode_unref(node);
            return -ENOTDIR;
        }

        LIST_FOREACH(mounts, n) {
            Mount* m = CONTAINER_OF(n, Mount, mounts_node);
            if (m->covered_node == node) {
                vnode_unref(node);
                return -EBUSY;
            }
        }
    }

    Mount* mount = kmalloc(sizeof(Mount));

    *mount = (Mount) {
        .ops = ops,
        .covered_node = node
    };

    strncpy(mount->fs_type, fs_type, sizeof(mount->fs_type) - 1);
    mount->fs_type[sizeof(mount->fs_type) - 1] = '\0';

    if (node != nullptr)
        vnode_ref(node);


    int res = mount->ops->mount(mount);
    if (res < 0) {
        if (node != nullptr) vnode_unref(node);
        kfree(mount);
        return res;
    }

    list_append(&mounts, &mount->mounts_node);

    return 0;
}

int vfs_split_path(const char *path, char* parent_buf, size_t parent_size, char* name_buf, size_t name_size) {
    size_t len = strlen(path);

    /* strip trailing slashes, but leave at least one */
    while (len > 1 && path[len-1] == '/')
        len--;

    /* find last slash in path[0..len) */
    size_t slash = (size_t)-1;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/')
            slash = i;
    }

    if (slash == (size_t)-1) {
        /* no slash at all: parent=".", name=path[0..len) */
        if (parent_size < 2 || name_size < len+1)
            return -ENAMETOOLONG;
        parent_buf[0] = '.';
        parent_buf[1] = '\0';
        memcpy(name_buf, path, len);
        name_buf[len] = '\0';
        return 0;
    }

    if (slash == 0) {
        /* parent is “/” */
        if (parent_size < 2 || name_size < (len-1)+1)
            return -ENAMETOOLONG;
        parent_buf[0] = '/';
        parent_buf[1] = '\0';
        memcpy(name_buf, path+1, len-1);
        name_buf[len-1] = '\0';
        return 0;
    }

    /* normal case: slash in middle */
    size_t parent_len = slash;
    size_t name_len   = len - slash - 1;
    if (parent_size < parent_len+1 || name_size < name_len+1)
        return -ENAMETOOLONG;

    memcpy(parent_buf, path, parent_len);
    parent_buf[parent_len] = '\0';

    memcpy(name_buf, path + slash + 1, name_len);
    name_buf[name_len] = '\0';
    return 0;
}

int vfs_root(VNode** root_node) {
    if (list_is_empty(&mounts))
        return -ENOENT;

    Mount* mount = CONTAINER_OF(list_peek(&mounts), Mount, mounts_node);
    return mount->ops->root(mount, root_node);
}

int vfs_lookup(VNode* start, const char* path, VNode** result) {
    VNode *current;
    VNode* next;
    char component[MAX_COMPONENT_LEN + 1];
    size_t pos = 0, comp_len;
    int err;

    /* 1. Initialize starting point */
    if (start == nullptr) {
        if ((err = vfs_root(&current)) < 0)
            return err;
    } else {
        current = start;
    }

    vnode_ref(current);

    /* skip leading slashes */
    while (path[pos] == '/')
        pos++;

    /* 2. Walk each component */
    while (path[pos] != '\0') {
        /* extract next component up to slash or end */
        comp_len = 0;
        while (path[pos + comp_len] != '\0' &&
            path[pos + comp_len] != '/') {
            if (comp_len < MAX_COMPONENT_LEN)
                component[comp_len] = path[pos + comp_len];
            comp_len++;
        }

        if (comp_len > MAX_COMPONENT_LEN)
            comp_len = MAX_COMPONENT_LEN;

        component[comp_len] = '\0';

        /* advance pos past the component */
        pos += comp_len;
        /* skip any slashes */
        while (path[pos] == '/')
            pos++;

        /* skip empty or “.” */
        if (comp_len == 0 || (comp_len == 1 && component[0] == '.'))
            continue;

        /* handle “..” */
        if (comp_len == 2 && component[0] == '.' && component[1] == '.') {
            if (current->is_root) {
                /* at this FS root: cross mount if covered */
                if (current->mount != nullptr && current->mount->covered_node != nullptr) {
                    next = current->mount->covered_node;
                    vnode_ref(next);
                } else {
                    /* global root – stay put */
                    next = current;
                    vnode_ref(next);
                }
            } else {
                /* delegate “..” to the filesystem */
                if ((err = current->ops->lookup(current, "..", &next)) < 0) {
                    vnode_unref(current);
                    return err;
                }
            }
        } else {
            /* normal component: ask FS to resolve */
            if ((err = current->ops->lookup(current, component, &next)) < 0) {
                vnode_unref(current);
                return err;
            }
        }

        /* drop old ref, move to next */
        vnode_unref(current);
        current = next;
    }

    /* 3. Return held reference */
    *result = current;
    return 0;
}

int vfs_create_file(VNode* start, const char* parent_path, const char* name, VNode** new) {
    VNode* parent;
    int err;

    if ((err = vfs_lookup(start, parent_path, &parent)) < 0)
        return err;

    if (parent->type != VNODE_TYPE_DIR) {
        vnode_unref(parent);
        return -ENOTDIR;
    }

    err = parent->ops->create_file(parent, name, new);
    vnode_unref(parent);

    return err;
}

int vfs_create_dir(VNode* start, const char* parent_path, const char* name, VNode** new) {
    VNode* parent;
    int err;

    if ((err = vfs_lookup(start, parent_path, &parent)) < 0)
        return err;

    if (parent->type != VNODE_TYPE_DIR) {
        vnode_unref(parent);
        return -ENOTDIR;
    }

    err = parent->ops->create_dir(parent, name, new);
    vnode_unref(parent);

    return err;
}

ssize_t vfs_read(VNode* start, const char* path, void* buf, size_t len, off_t off) {
    VNode* node;
    ssize_t res;

    if ((res = vfs_lookup(start, path, &node)) < 0)
        return res;

    res = node->ops->read(node, buf, len, off);
    vnode_unref(node);

    return res;
}

ssize_t vfs_write(VNode* start, const char* path, const void* buf, size_t len, off_t off) {
    VNode* node;
    ssize_t res;

    if ((res = vfs_lookup(start, path, &node)) < 0)
        return res;

    res = node->ops->write(node, buf, len, off);
    vnode_unref(node);

    return res;
}

int vfs_get_attr(VNode* start, const char* path, Attributes* attr) {
    VNode* node;
    ssize_t res;

    if ((res = vfs_lookup(start, path, &node)) < 0)
        return res;

    res = node->ops->get_attr(node, attr);
    vnode_unref(node);

    return res;
}
