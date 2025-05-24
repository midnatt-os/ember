#include "fs/vfs.h"

#include <stdint.h>

#include "common/assert.h"
#include "common/log.h"
#include "lib/string.h"
#include "memory/heap.h"
#include "common/lock/spinlock.h"


static List vfs_list = LIST_NEW;
static Spinlock vfs_list_lock = SPINLOCK_NEW;

static bool split_path(char* path, char** parent, char** base) {
    size_t len = strlen(path);
    int sep_idx = -1;

    // Strip trailing '/' unless path is "/"
    while (len > 1 && path[len - 1] == '/')
        path[--len] = '\0';

    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') {
            sep_idx = i;
            break;
        }
    }

    if (sep_idx == -1)
        return false;

    if (sep_idx == 0) {
        *parent = "/";
    } else {
        size_t parent_len = sep_idx;
        *parent = kmalloc(parent_len + 1);
        strncpy(*parent, path, parent_len);
    }

    char* base_start = path + sep_idx + 1;
    size_t base_len = len - (sep_idx + 1);

    *base = kmalloc(base_len + 1);
    strncpy(*base, base_start, base_len);

    return true;
}

VfsResult vfs_lookup(char* path, VNode** found_node) {
    size_t path_len = strlen(path);

    Vfs* found_fs = nullptr;
    size_t longest_match = 0;

    spinlock_acquire(&vfs_list_lock);

    LIST_FOREACH(vfs_list, node) {
        Vfs* fs = LIST_ELEMENT_OF(node, Vfs, list_node);
        char* mp = fs->mount_point;
        size_t mp_len = strlen(mp);

        if (mp_len > path_len)
            continue;

        size_t len = 0;
        while (mp[len] == path[len] && mp[len] != '\0' && path[len] != '\0') len++;

        // TODO: Yeah uh, what the fuck?
        // allow "/" to match any absolute path,
        // otherwise only accept a slash or end-of-string boundary
        if (mp[len] == '\0' && (mp_len == 1 || path[len] == '/' || path[len] == '\0')) {
            if (len > longest_match) {
                found_fs = fs;
                longest_match = len;
            }
        }
    }

    spinlock_release(&vfs_list_lock);

    if (found_fs == nullptr)
        return VFS_RES_NOT_FOUND;

    char* subpath = path + longest_match;
    if (subpath[0] == '/') subpath++;

    VNode* current = nullptr;
    if (found_fs->ops->root(found_fs, &current) != VFS_RES_OK) {
        return VFS_RES_ERR;
    }

    if (subpath[0] == '\0') {
        *found_node = current;
        return VFS_RES_OK;
    }

    while (subpath[0] != '\0') {
        // Get component.
        size_t len = 0;
        while (subpath[len] != '/' && subpath[len] != '\0') len++;

        char* comp = kmalloc(len + 1);
        strncpy(comp, subpath, len);
        comp[len] = '\0';

        // Lookup next vnode.
        VNode* next = nullptr;
        VfsResult res = current->ops->lookup(current, comp, &next);
        kfree(comp);

        if (res != VFS_RES_OK)
            return res;

        current = next;

        // Skip to next component.
        if (subpath[len] == '/')
            subpath += len + 1;
        else
            subpath += len;
    }

    *found_node = current;
    return VFS_RES_OK;
}

VfsResult vfs_read_dir(char* path, size_t* offset, DirEntry* dir_entry) {
    VNode* found_node = nullptr;
    VfsResult res = vfs_lookup(path, &found_node);
    if (res != VFS_RES_OK)
        return res;

    if (found_node->type != VNODE_DIR)
        return VFS_RES_NOT_DIR;

    return found_node->ops->read_dir(found_node, offset, dir_entry);
}

VfsResult vfs_create_file(char* path, VNode** new_node) {
    char* parent_path;
    char* name;
    ASSERT(split_path(path, &parent_path, &name)); // TODO: assert uhh???

    VNode* parent_node;
    VfsResult lookup_res = vfs_lookup(parent_path, &parent_node);
    kfree(parent_path);

    if (lookup_res != VFS_RES_OK)
        return lookup_res;

    VfsResult creation_res = parent_node->ops->create_file(parent_node, name, new_node);
    kfree(name);

    return creation_res;
}

// TODO: Should create file/dir take in a path and name instead of only path and doing to splitting in the vfs code?
VfsResult vfs_create_dir(char* path, VNode** new_node) {
    char* parent_path;
    char* name;
    ASSERT(split_path(path, &parent_path, &name)); // TODO: assert uhh???

    VNode* parent_node;
    VfsResult lookup_res = vfs_lookup(parent_path, &parent_node);
    kfree(parent_path);

    if (lookup_res != VFS_RES_OK)
        return lookup_res;

    VfsResult creation_res = parent_node->ops->create_dir(parent_node, name, new_node);
    kfree(name);

    return creation_res;
}

VfsResult vfs_write(char* path, void* buf, size_t off, size_t len, size_t* written) {
    VNode* node;
    VfsResult lookup_res = vfs_lookup(path, &node);
    if (lookup_res != VFS_RES_OK)
        return lookup_res;

    return node->ops->write(node, buf, off, len, written);
}

VfsResult vfs_read(char* path, void* buf, size_t off, size_t len, size_t* read) {
    VNode* node;
    VfsResult lookup_res = vfs_lookup(path, &node);
    if (lookup_res != VFS_RES_OK)
        return lookup_res;

    return node->ops->read(node, buf, off, len, read);
}

VfsResult vfs_get_attr(char* path, VNodeAttributes* attr) {
    VNode* node;
    VfsResult lookup_res = vfs_lookup(path, &node);
    if (lookup_res != VFS_RES_OK)
        return lookup_res;

    return node->ops->get_attr(node, attr);
}

VfsResult vfs_mount(char* path, VfsOps* ops) {
    bool is_root = streq(path, "/");

    spinlock_acquire(&vfs_list_lock);

    if (!(is_root && list_is_empty(&vfs_list))) {
        VNode* mount_node = nullptr;
        VfsResult res = vfs_lookup(path, &mount_node);
        if (res != VFS_RES_OK) {
            spinlock_release(&vfs_list_lock);
            return res;
        }

        if (mount_node->type != VNODE_DIR) {
            spinlock_release(&vfs_list_lock);
            return VFS_RES_NOT_DIR;
        }
    }

    spinlock_release(&vfs_list_lock);

    Vfs* vfs = kmalloc(sizeof(Vfs));
    char* mount_point = kmalloc(strlen(path) + 1);
    strcpy(mount_point, path);

    vfs->mount_point = mount_point;
    vfs->ops = ops;

    VfsResult res = vfs->ops->mount(vfs);
    if (res != VFS_RES_OK) {
        kfree(vfs);
        kfree(mount_point);
        return res;
    }

    list_prepend(&vfs_list, &vfs->list_node);
    return VFS_RES_OK;
}
