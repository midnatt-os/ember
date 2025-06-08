#include "fs/vnode.h"
#include "lib/ref_count.h"
#include "memory/heap.h"



void vnode_ref(VNode* node) {
    ref_count_inc(&node->ref_count);
}

void vnode_unref(VNode* node) {
    if (ref_count_dec(&node->ref_count) != 0)
        return;

    node->ops->free(node);
}
