#include "fs/devfs/devfs.h"
#include "common/assert.h"
#include "common/log.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "lib/container.h"
#include "lib/list.h"
#include "memory/heap.h"
#include "lib/string.h"
#include "abi/errno.h"

#define DEVFS_CTX(VFS) ((DevfsInfo*) (VFS)->data)
#define DEVFS_NODE(VNODE) ((DevfsNode*) (VNODE)->data)

typedef struct DevfsNode {
    char name[MAX_NAME_LEN+1];
    VNode* vnode;

    union {
        struct { List children; } dir;
        struct { DeviceOps* dev_ops; void* ctx; } device;
    };

    struct DevfsNode* parent;

    List children;
    ListNode list_node;
} DevfsNode;

typedef struct {
    DevfsNode* root;
} DevfsInfo;



DevfsInfo* devfs_info = nullptr;
static VNodeOps devfs_node_ops;

static DevfsNode* create_node(Mount* mount, const char* name, VNodeType type, DeviceOps* dev_ops) {
    ASSERT(type == VNODE_TYPE_DIR || type == VNODE_TYPE_CHAR_DEV);
    VNode* vnode = kmalloc(sizeof(VNode));

    *vnode = (VNode) {
        .mount = mount,
        .ops = &devfs_node_ops,
        .type = type,
        .lock = MUTEX_NEW,
        .ref_count = 0
    };

    DevfsNode* node = kmalloc(sizeof(DevfsNode));
    strncpy((char*) node->name, name, strlen(name)+1);

    if (type == VNODE_TYPE_DIR) {
        ASSERT(dev_ops == nullptr);
        node->dir.children = LIST_NEW;
    } else {
        node->device.dev_ops = dev_ops;
        node->device.ctx = nullptr;
    }

    node->vnode = vnode;
    vnode->data = node;

    return node;
}

static int devfs_mount(Mount* mount) {
    DevfsInfo* info = kmalloc(sizeof(DevfsInfo));

    mount->data = info;
    info->root = create_node(mount, "root", VNODE_TYPE_DIR, nullptr);
    info->root->vnode->is_root = true;

    devfs_info = info;

    return 0;
}

static int devfs_root(Mount* mount, VNode** node) {
    *node = DEVFS_CTX(mount)->root->vnode;
    vnode_ref(*node);
    return 0;
}

MountOps devfs_ops = { .mount = devfs_mount, .root = devfs_root };

void devfs_register(const char* parent_path, const char* name, VNodeType type, DeviceOps* dev_ops, void* dev_ctx) {
    ASSERT(type == VNODE_TYPE_CHAR_DEV);
    ASSERT(dev_ops != nullptr);

    VNode* vnode;
    ASSERT(vfs_create_file(nullptr, parent_path, name, &vnode) == 0);

    DevfsNode* dev_node = DEVFS_NODE(vnode);

    dev_node->device.dev_ops = dev_ops;
    dev_node->device.ctx = dev_ctx;

    logln(LOG_INFO, "DEVFS", "Registered device: %s/%s", parent_path, name);
}

static DevfsNode* find_in_dir(DevfsNode* dir_node, const char* name) {
    ASSERT(dir_node->vnode->type == VNODE_TYPE_DIR);

    if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
        return dir_node->parent;

    LIST_FOREACH(dir_node->dir.children, n) {
        DevfsNode* node = CONTAINER_OF(n, DevfsNode, list_node);

        if (streq(node->name, name))
            return node;
    }

    return nullptr;
}

static int devfs_lookup(VNode* node, const char* name, VNode** found) {
    if (node->type != VNODE_TYPE_DIR)
        return -ENOTDIR;

    DevfsNode* match = find_in_dir(DEVFS_NODE(node), name);
    if (match == nullptr)
        return -ENOENT;

    vnode_ref(match->vnode);
    *found = match->vnode;

    return 0;
}

void devfs_free([[maybe_unused]] VNode* node) {
    //logln(LOG_DEBUG, "TMPFS", "tmpfs_free(%#p (%s))", node, TMPFS_NODE(node)->name);
}

ssize_t devfs_read(VNode* node, void* buf, size_t len, off_t off) {
    DevfsNode* n = DEVFS_NODE(node);

    if (node->type == VNODE_TYPE_DIR)
        return -EISDIR;

    ASSERT(n->device.dev_ops->read != nullptr);

    return n->device.dev_ops->read(n->device.ctx, buf, len, off);
}

ssize_t devfs_write(VNode* node, const void* buf, size_t len, off_t off) {
    DevfsNode *n = DEVFS_NODE(node);

    if (node->type == VNODE_TYPE_DIR)
        return -EISDIR;

    if (n->device.dev_ops->write == nullptr)
        return -ENOSYS;

    return n->device.dev_ops->write(n->device.ctx, buf, len, off);
}

int devfs_create_file(VNode* parent, const char* name, VNode** new) {
    log_raw("devfs creating file: %s\n", name);
    if (parent->type != VNODE_TYPE_DIR)
        return -ENOTDIR;

    DevfsNode* dir = DEVFS_NODE(parent);

    DevfsNode* child = create_node(parent->mount, name, VNODE_TYPE_CHAR_DEV, nullptr);

    child->parent = dir;
    list_append(&dir->dir.children, &child->list_node);

    vnode_ref(child->vnode);
    *new = child->vnode;

    return 0;
}

int devfs_create_dir(VNode* parent, const char* name, VNode** new) {
    log_raw("devfs creating dir: %s\n", name);
    if (parent->type != VNODE_TYPE_DIR)
        return -ENOTDIR;

    DevfsNode* dir = DEVFS_NODE(parent);

    DevfsNode* child = create_node(parent->mount, name, VNODE_TYPE_DIR, nullptr);

    child->parent = dir;
    list_append(&dir->dir.children, &child->list_node);

    vnode_ref(child->vnode);
    *new = child->vnode;

    return 0;
}

int devfs_get_attr(VNode* node, Attributes* attr) {
    [[maybe_unused]] DevfsNode* n = DEVFS_NODE(node);

    *attr = (Attributes) {
        .size = 0,
    };

    return 0;
}

int devfs_ioctl(VNode* node, uint64_t request, void* argp) {
    DevfsNode* n = DEVFS_NODE(node);
    if (node->type == VNODE_TYPE_DIR)
        return -EISDIR;

    return n->device.dev_ops->ioctl(n->device.ctx, request, argp);
}

// TODO: handle "stubs" correctly...
static VNodeOps devfs_node_ops = {
    .lookup      = devfs_lookup,
    .free        = devfs_free,
    .create_file = devfs_create_file,
    .create_dir  = devfs_create_dir,
    .read        = devfs_read,
    .write       = devfs_write,
    .get_attr    = devfs_get_attr,
    .ioctl       = devfs_ioctl,
    //.read_dir    = tmpfs_read_dir,
};
