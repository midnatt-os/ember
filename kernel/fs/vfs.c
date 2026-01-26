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

// We only need one index tracking variable now
const char* extract_component(const char* path, uint64_t* cursor) {
    // 1. Skip leading slashes
    while (path[*cursor] == '/') {
        (*cursor)++;
    }

    // 2. Check for End of String
    // If path was "/", we are now at '\0' and return NULL.
    if (path[*cursor] == '\0') {
        return nullptr;
    }

    // 3. Mark the start of the actual name
    uint64_t start = *cursor;

    // 4. Find the end of this component
    while (path[*cursor] != '/' && path[*cursor] != '\0') {
        (*cursor)++;
    }

    uint64_t end = *cursor;
    uint64_t len = end - start;

    // 5. Allocate and Copy
    char* component = heap_alloc(len + 1);
    memcpy(component, path + start, len);
    component[len] = '\0';

    return component;
}

// Helper to check if a vnode is the root of its specific mount structure
static bool is_mount_root(vnode_t* vn) {
    if (!vn || !vn->mount)
        return false;
    // If the vnode's list of mounted filesystems points to itself via root ops
    // OR more simply: checking if the mount structure's root vnode is this vnode.
    // However, usually we need to ask the filesystem or check cached state.
    // Based on your struct:
    // vnode->mount points to the mount_t this vnode belongs to.
    // mount_t->root points to the root vnode of that fs.
    return vn->mount->root == vn;
}

int vfs_lookup(path_t path, vnode_t** result) {
    vnode_t* current_vnode;

    // 1. Resolve Start Node
    if (!path.base || is_slash(path.path[0])) {
        current_vnode = vfs_get_root();
        if (!current_vnode)
            return -ENOENT;
    } else {
        current_vnode = path.base;
    }

    // 2. Setup Loop Variables
    uint64_t cursor = 0;
    const char* name = nullptr;

    // 3. Iterate using the new single-cursor helper
    // If path is "/", extract_component returns NULL immediately.
    while ((name = extract_component(path.path, &cursor)) != nullptr) {
        // Handle "." (Current Directory)
        if (strcmp(name, ".") == 0) {
            heap_free((void*) name, strlen(name) + 1);
            continue;
        }

        // Handle ".." (Parent Directory)
        if (strcmp(name, "..") == 0) {
            if (is_mount_root(current_vnode)) {
                if (current_vnode->mount->covered_vnode) {
                    current_vnode = current_vnode->mount->covered_vnode;
                } else {
                    // Global root .. stays at global root
                    heap_free((void*) name, strlen(name) + 1);
                    continue;
                }
            }
            // Fall through to normal lookup for ".."
        }

        if (current_vnode->type != V_DIR) {
            heap_free((void*) name, strlen(name) + 1);
            return -ENOTDIR;
        }

        // 4. Perform the Lookup
        vnode_t* next_vnode = nullptr;
        int err = current_vnode->ops->lookup(current_vnode, name, &next_vnode);

        heap_free((void*) name, strlen(name) + 1);

        if (err != 0)
            return err;

        // 5. Handle Mount Crossing
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
    const char* last = NULL;
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

    // 1. Copy the path so we can modify it (to split string)
    size_t path_len = strlen(path.path);
    char* buf = heap_alloc(path_len + 1);
    if (!buf)
        return -ENOMEM;
    memcpy(buf, path.path, path_len + 1);

    char* last_slash = str_rchr(buf, '/');
    char* filename = nullptr;
    vnode_t* parent_dir = nullptr;

    // 2. Split into Directory and Filename
    if (last_slash) {
        // CASE A: Path contains slashes (e.g. "/tmp/file" or "dir/file")
        *last_slash = '\0'; // Cut string at the slash
        filename = last_slash + 1;

        // If path was "/file", parent path is "" (empty).
        // If path was "/tmp/file", parent path is "/tmp".

        // Use existing path logic, but point to our modified buffer
        path_t dir_path = { .base = path.base, .path = buf };

        // Handle absolute path edge case: "/file" -> parent is "/"
        // vfs_lookup handles "" by returning the base/root, so this works.
        // However, if it was ABS_PATH("/file"), base is NULL.
        // vfs_lookup(NULL, "") -> vfs_root. Correct.

        int err = vfs_lookup(dir_path, &parent_dir);
        if (err) {
            heap_free(buf, path_len + 1);
            return err;
        }
    } else {
        // CASE B: No slashes (e.g. "file.txt")
        filename = buf;

        // Parent is simply the base of the lookup
        if (path.base) {
            parent_dir = path.base;
        } else {
            // If ABS_PATH("file.txt") was passed, implies root
            parent_dir = vfs_get_root();
        }
    }

    // 3. Validation
    if (!parent_dir) {
        heap_free(buf, path_len + 1);
        return -ENOENT;
    }

    if (parent_dir->type != V_DIR) {
        heap_free(buf, path_len + 1);
        return -ENOTDIR;
    }

    if (strlen(filename) == 0) {
        // Path ended in slash? (e.g. "/tmp/")
        heap_free(buf, path_len + 1);
        return -EISDIR; // Cannot create a directory via vfs_create
    }

    // 4. Delegate to Filesystem
    int err = parent_dir->ops->create(parent_dir, filename);

    heap_free(buf, path_len + 1);
    return err;
}

int vfs_mkdir(path_t path) {
    if (!path.path)
        return -EINVAL;

    // 1. Copy the path so we can modify it
    size_t path_len = strlen(path.path);
    char* buf = heap_alloc(path_len + 1);
    memcpy(buf, path.path, path_len + 1);

    char* last_slash = str_rchr(buf, '/');
    char* dirname = nullptr;
    vnode_t* parent_dir = nullptr;

    // 2. Split into Parent Path and New Directory Name
    if (last_slash) {
        *last_slash = '\0'; // Cut string at the slash
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

    // 3. Validation
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
        return -EEXIST; // Path was something like "/path/to/"
    }

    // 4. Delegate to Filesystem
    int err = parent_dir->ops->mkdir(parent_dir, dirname);

    heap_free(buf, path_len + 1);
    return err;
}

ssize_t vfs_read(vnode_t* file, void* buffer, size_t count, off_t offset) {
    if (!file || !buffer)
        return -EINVAL;

    // You can't read from a directory using standard file read()
    if (file->type == V_DIR)
        return -EISDIR;

    // Check if the filesystem actually supports reading
    if (!file->ops->read)
        return -ENOSYS;

    // Delegate to the specific filesystem (e.g., tmpfs)
    return file->ops->read(file, buffer, count, offset);
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
