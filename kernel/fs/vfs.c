#include "fs/vfs.h"

#include "common/assert.h"
#include "common/lock/mutex.h"
#include "common/log.h"
#include "lib/container.h"
#include "lib/hashmap.h"
#include "lib/mem.h"
#include "lib/string.h"
#include "mem/heap.h"

#include <common/errno.h>
#include <stddef.h>
#include <stdint.h>

#define FSTYPES_BUCKETS 64
static hashmap_node_t* fs_types_buckets[FSTYPES_BUCKETS];
static hashmap_t fs_types;
static mutex_t fs_types_lock = MUTEX_NEW;

static size_t hash_fstype(const void* key) {
    const char* s = key;
    size_t h = (size_t) 1469598103934665603ull;
    while (*s) {
        h ^= (size_t) *s++;
        h *= (size_t) 1099511628211ull;
    }
    return h;
}

static bool fstype_eq(const hashmap_node_t* node, const void* key) {
    const vfs_fstype_t* fs = CONTAINER_OF(node, vfs_fstype_t, node);
    return streq(fs->name, (const char*) key);
}

int vfs_register(vfs_fstype_t* fs_type) {
    mutex_lock(&fs_types_lock);
    bool ok = hashmap_insert(&fs_types, fs_type->name, &fs_type->node);
    mutex_unlock(&fs_types_lock);

    if (!ok)
        return -EEXIST;

    logln(LOG_INFO, "VFS", "registered fstype: %s", fs_type->name);

    return 0;
}

vnode_t* vfs_root;

vnode_t* vfs_get_root() {
    // TODO: LOCK?
    return vfs_root;
}

void vfs_set_root(vnode_t* new_root) {
    // TODO: LOCK?
    vfs_root = new_root;
}

static inline bool is_slash(char c) {
    return c == '/';
}

const char* extract_component(const char* path, uint64_t* cursor) {
    while (path[*cursor] == '/') {
        (*cursor)++;
    }

    if (path[*cursor] == '\0') {
        return nullptr;
    }

    uint64_t start = *cursor;

    while (path[*cursor] != '/' && path[*cursor] != '\0') {
        (*cursor)++;
    }

    uint64_t end = *cursor;
    uint64_t len = end - start;

    char* component = heap_alloc(len + 1);
    memcpy(component, path + start, len);
    component[len] = '\0';

    return component;
}

static bool is_mount_root(vnode_t* vn) {
    if (!vn || !vn->mount)
        return false;
    return vn->mount->root == vn;
}

int vfs_lookup(path_t path, vnode_t** result) {
    vnode_t* current_vnode;

    if (!path.base || is_slash(path.path[0])) {
        current_vnode = vfs_get_root();
        if (!current_vnode)
            return -ENOENT;
    } else {
        current_vnode = path.base;
    }

    uint64_t cursor = 0;
    const char* name = nullptr;

    while ((name = extract_component(path.path, &cursor)) != nullptr) {
        if (strcmp(name, ".") == 0) {
            heap_free((void*) name, strlen(name) + 1);
            continue;
        }

        if (strcmp(name, "..") == 0) {
            if (is_mount_root(current_vnode)) {
                if (current_vnode->mount->covered_vnode) {
                    current_vnode = current_vnode->mount->covered_vnode;
                } else {
                    heap_free((void*) name, strlen(name) + 1);
                    continue;
                }
            }
        }

        if (current_vnode->type != V_DIR) {
            heap_free((void*) name, strlen(name) + 1);
            return -ENOTDIR;
        }

        vnode_t* next_vnode = nullptr;
        int err = current_vnode->ops->lookup(current_vnode, name, &next_vnode);

        heap_free((void*) name, strlen(name) + 1);

        if (err != 0)
            return err;

        while (next_vnode->covering_mount) {
            vnode_t* mounted_root = next_vnode->covering_mount->root;
            if (!mounted_root)
                return -ENOENT;
            next_vnode = mounted_root;
        }

        current_vnode = next_vnode;
    }

    *result = current_vnode;
    return 0;
}

static char* str_rchr(const char* s, int c) {
    const char* last = nullptr;
    while (*s) {
        if (*s == c)
            last = s;
        s++;
    }
    return (char*) last;
}

int vfs_create(path_t path) {
    if (!path.path)
        return -EINVAL;

    size_t path_len = strlen(path.path);
    char* buf = heap_alloc(path_len + 1);
    if (!buf)
        return -ENOMEM;
    memcpy(buf, path.path, path_len + 1);

    char* last_slash = str_rchr(buf, '/');
    char* filename = nullptr;
    vnode_t* parent_dir = nullptr;

    if (last_slash) {
        *last_slash = '\0';
        filename = last_slash + 1;


        path_t dir_path = { .base = path.base, .path = buf };

        int err = vfs_lookup(dir_path, &parent_dir);
        if (err) {
            heap_free(buf, path_len + 1);
            return err;
        }
    } else {
        filename = buf;

        if (path.base) {
            parent_dir = path.base;
        } else {
            parent_dir = vfs_get_root();
        }
    }

    if (!parent_dir) {
        heap_free(buf, path_len + 1);
        return -ENOENT;
    }

    if (parent_dir->type != V_DIR) {
        heap_free(buf, path_len + 1);
        return -ENOTDIR;
    }

    if (strlen(filename) == 0) {
        heap_free(buf, path_len + 1);
        return -EISDIR;
    }

    int err = parent_dir->ops->create(parent_dir, filename);

    heap_free(buf, path_len + 1);
    return err;
}

int vfs_mkdir(path_t path) {
    if (!path.path)
        return -EINVAL;

    size_t path_len = strlen(path.path);
    char* buf = heap_alloc(path_len + 1);
    memcpy(buf, path.path, path_len + 1);

    char* last_slash = str_rchr(buf, '/');
    char* dirname = nullptr;
    vnode_t* parent_dir = nullptr;

    if (last_slash) {
        *last_slash = '\0';
        dirname = last_slash + 1;

        path_t dir_path = { .base = path.base, .path = buf };

        int err = vfs_lookup(dir_path, &parent_dir);
        if (err) {
            heap_free(buf, path_len + 1);
            return err;
        }
    } else {
        dirname = buf;
        parent_dir = path.base ? path.base : vfs_get_root();
    }

    if (!parent_dir) {
        heap_free(buf, path_len + 1);
        return -ENOENT;
    }

    if (parent_dir->type != V_DIR) {
        heap_free(buf, path_len + 1);
        return -ENOTDIR;
    }

    if (strlen(dirname) == 0) {
        heap_free(buf, path_len + 1);
        return -EEXIST;
    }

    int err = parent_dir->ops->mkdir(parent_dir, dirname);

    heap_free(buf, path_len + 1);
    return err;
}

ssize_t vfs_read(vnode_t* vnode, void* buffer, size_t count, off_t offset) {
    if (!vnode || !buffer)
        return -EINVAL;

    if (vnode->type == V_DIR)
        return -EISDIR;

    return vnode->ops->read(vnode, buffer, count, offset);
}

ssize_t vfs_write(vnode_t* vnode, const void* buffer, size_t count, off_t offset) {
    if (!vnode || !buffer)
        return -EINVAL;

    if (vnode->type == V_DIR)
        return -EISDIR;

    if (!vnode->ops->write)
        return -ENOSYS;

    return vnode->ops->write(vnode, buffer, count, offset);
}

int vfs_getattr(vnode_t* node, stat_t* st) {
    if (!node || !st)
        return -EINVAL;

    return node->ops->getattr(node, st);
}

int vfs_symlink(path_t linkpath, const char* target) {
    if (!linkpath.path || !target)
        return -EINVAL;

    size_t path_len = strlen(linkpath.path);
    char* buf = heap_alloc(path_len + 1);
    if (!buf)
        return -ENOMEM;
    memcpy(buf, linkpath.path, path_len + 1);

    char* last_slash = str_rchr(buf, '/');
    char* name = nullptr;
    vnode_t* parent_dir = nullptr;

    if (last_slash) {
        *last_slash = '\0';
        name = last_slash + 1;

        // Special-case absolute paths like "/foo":
        // after splitting, buf becomes "" but we want parent to resolve to "/".
        if (buf[0] == '\0' && linkpath.path[0] == '/') {
            buf[0] = '/';
            buf[1] = '\0';
        }

        path_t dir_path = { .base = linkpath.base, .path = buf };
        int err = vfs_lookup(dir_path, &parent_dir);
        if (err) {
            heap_free(buf, path_len + 1);
            return err;
        }
    } else {
        name = buf;
        parent_dir = linkpath.base ? linkpath.base : vfs_get_root();
    }

    if (!parent_dir) {
        heap_free(buf, path_len + 1);
        return -ENOENT;
    }

    if (parent_dir->type != V_DIR) {
        heap_free(buf, path_len + 1);
        return -ENOTDIR;
    }

    if (!parent_dir->ops || !parent_dir->ops->symlink) {
        heap_free(buf, path_len + 1);
        return -ENOSYS;
    }

    // Path ends with '/' -> empty last component
    if (strlen(name) == 0) {
        heap_free(buf, path_len + 1);
        return -EISDIR;
    }

    int err = parent_dir->ops->symlink(parent_dir, name, target);

    heap_free(buf, path_len + 1);
    return err;
}


ssize_t vfs_readlink(vnode_t* link, char* buf, size_t buflen) {
    /*
        You’ll almost certainly also want a path-based helper eventually:
            - lookup with nofollow last component
            - check it’s V_LNK
            - call vfs_readlink(vn, ...)
     */
    if (!link || !buf)
        return -EINVAL;

    if (link->type != V_LNK)
        return -EINVAL;

    if (!link->ops || !link->ops->readlink)
        return -ENOSYS;

    // POSIX: returns number of bytes placed in buf (not NUL-terminated)
    return link->ops->readlink(link, buf, buflen);
}


int vfs_mount(const char* fstype_name, const char* target_path) {
    hashmap_node_t* fstype_node = hashmap_find(&fs_types, fstype_name);
    if (!fstype_node)
        return -ENOENT;

    const mount_ops_t* ops = CONTAINER_OF(fstype_node, vfs_fstype_t, node)->ops;

    mount_t* m = heap_alloc(sizeof(mount_t));
    *m = (mount_t) { .ops = ops };

    vnode_t* target_vnode = nullptr;

    if (vfs_get_root() != nullptr) {
        int err = vfs_lookup(ABS_PATH(target_path), &target_vnode);
        if (err < 0) {
            heap_free(m, sizeof(mount_t));
            return err;
        }

        if (target_vnode->type != V_DIR) {
            heap_free(m, sizeof(mount_t));
            return -ENOTDIR;
        }

        if (target_vnode->covering_mount != nullptr) {
            heap_free(m, sizeof(mount_t));
            return -EBUSY;
        }
    }

    int err = ops->mount(m);
    ASSERT(m->root);
    if (err < 0) {
        heap_free(m, sizeof(mount_t));
        return err;
    }

    m->root->mount = m;

    if (target_vnode) {
        m->covered_vnode = target_vnode;
        target_vnode->covering_mount = m;
    } else {
        vfs_set_root(m->root);
    }

    logln(LOG_INFO, "VFS", "Mounted \"%s\" on %s", fstype_name, target_path);
    return 0;
}

void vfs_init() {
    ASSERT(hashmap_init(&fs_types, fs_types_buckets, FSTYPES_BUCKETS, hash_fstype, fstype_eq));
}
