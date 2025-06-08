#pragma once

#include "common/types.h"
#include "common/lock/mutex.h"
#include "lib/ref_count.h"
#include <stddef.h>

typedef enum {
    VNODE_TYPE_FILE,
    VNODE_TYPE_DIR,
    VNODE_TYPE_SYMLINK
} VNodeType;

typedef struct {
    size_t size;
} Attributes;

typedef struct VNode VNode;
typedef struct Mount Mount;

typedef struct {
    int (*lookup)(VNode* node, const char* name, VNode** found);
    void (*free)(VNode* node);
    int (*create_file)(VNode* parent, const char* name, VNode** new);
    int (*create_dir)(VNode* parent, const char* name, VNode** new);
    ssize_t (*read)(VNode* node, void* buf, size_t len, off_t off);
    ssize_t (*write)(VNode* node, const void* buf, size_t len, off_t off);
    int (*get_attr)(VNode* node, Attributes* attr);
} VNodeOps;

struct VNode {
    VNodeType type;
    bool is_root;
    VNodeOps* ops;
    Mount* mount;
    void* data;

    ref_t ref_count;
    Mutex lock;
};

void vnode_ref(VNode* node);
void vnode_unref(VNode* node);
