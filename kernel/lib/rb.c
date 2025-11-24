#include "lib/rb.h"
// TODO: Maybe switch to NIL sentinel nodes instead of nullptr?

#include "common/assert.h"
#include "common/log.h"

#include <stddef.h>

// RED = true; BLACK = false
#define RED true
#define BLACK false
#define IS_RED(NODE) ((NODE)->color)
#define IS_BLACK(NODE) (!(NODE)->color)

void rb_tree_init(rb_tree_t* tree, rb_value_t (*value)(const rb_node_t* n)) {
    tree->value = value;
    tree->count = 0;
    tree->nil_node = (rb_node_t) { 0 };
    tree->nil = &tree->nil_node;
    tree->root = tree->nil;

    tree->nil_node.parent = tree->nil;
    tree->nil_node.left = tree->nil;
    tree->nil_node.right = tree->nil;
    tree->nil_node.color = BLACK;
}

static inline bool rb_node_is_linked_in(const rb_tree_t* tree, const rb_node_t* node) {
    if (!node)
        return false;
    if (node->parent == NULL && node->left == NULL && node->right == NULL)
        return false;
    if (node == tree->root)
        return true;
    return node->parent != tree->nil || node->left != tree->nil || node->right != tree->nil;
}

static inline rb_node_t* grandparent(rb_tree_t* tree, rb_node_t* n) {
    return n->parent == tree->nil ? tree->nil : n->parent->parent;
}

static inline rb_node_t* sibling(rb_tree_t* tree, rb_node_t* n) {
    if (n->parent == tree->nil)
        return tree->nil;

    return n == n->parent->left ? n->parent->right : n->parent->left;
}

static inline rb_node_t* uncle(rb_tree_t* tree, rb_node_t* n) {
    rb_node_t* g = grandparent(tree, n);

    return g == tree->nil ? tree->nil : sibling(tree, n->parent);
}

static void transplant(rb_tree_t* tree, rb_node_t* u, rb_node_t* v) {
    if (u->parent == tree->nil)
        tree->root = v;
    else if (u == u->parent->left)
        u->parent->left = v;
    else
        u->parent->right = v;

    v->parent = u->parent;
}

rb_node_t* rb_minimum(rb_tree_t* tree, rb_node_t* node) {
    while (node->left != tree->nil)
        node = node->left;
    return node;
}

rb_node_t* rb_maximum(rb_tree_t* tree, rb_node_t* node) {
    while (node->right != tree->nil)
        node = node->right;
    return node;
}

rb_node_t* rb_predecessor(rb_tree_t* tree, rb_node_t* node) {
    if (node == tree->nil)
        return tree->nil;

    if (node->left != tree->nil)
        return rb_maximum(tree, node->left);

    rb_node_t* p = node->parent;
    while (p != tree->nil && node == p->left) {
        node = p;
        p = p->parent;
    }

    return p;
}

rb_node_t* rb_successor(rb_tree_t* tree, rb_node_t* node) {
    if (node->right != tree->nil)
        return rb_minimum(tree, node->right);

    rb_node_t* p = node->parent;
    while (p != tree->nil && node == p->right) {
        node = p;
        p = p->parent;
    }

    return p;
}

static void rotate_left(rb_tree_t* tree, rb_node_t* x) {
    rb_node_t* y = x->right;
    x->right = y->left;

    if (y->left != tree->nil)
        y->left->parent = x;

    y->parent = x->parent;

    if (x->parent == tree->nil)
        tree->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;

    y->left = x;
    x->parent = y;
}

static void rotate_right(rb_tree_t* tree, rb_node_t* y) {
    rb_node_t* x = y->left;
    y->left = x->right;

    if (x->right != tree->nil)
        x->right->parent = y;

    x->parent = y->parent;

    if (y->parent == tree->nil)
        tree->root = x;
    else if (y == y->parent->left)
        y->parent->left = x;
    else
        y->parent->right = x;

    x->right = y;
    y->parent = x;
}

static void insert_fixup(rb_tree_t* tree, rb_node_t* z) {
    while (z->parent != tree->nil && IS_RED(z->parent)) {
        rb_node_t* g = grandparent(tree, z);

        if (z->parent == g->left) {
            rb_node_t* y = uncle(tree, z);

            if (IS_RED(y)) {
                z->parent->color = BLACK;
                y->color = BLACK;
                g->color = RED;
                z = g;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    rotate_left(tree, z);
                }

                z->parent->color = BLACK;
                g->color = RED;
                rotate_right(tree, g);
            }
        } else {
            rb_node_t* y = uncle(tree, z);

            if (IS_RED(y)) {
                z->parent->color = BLACK;
                y->color = BLACK;
                g->color = RED;
                z = g;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rotate_right(tree, z);
                }

                z->parent->color = BLACK;
                g->color = RED;
                rotate_left(tree, g);
            }
        }
    }

    tree->root->color = BLACK;
}

void rb_insert(rb_tree_t* tree, rb_node_t* node) {
    tree->count++;
    *node = (rb_node_t) {
        .left = tree->nil,
        .right = tree->nil,
        .parent = tree->nil,
        .color = RED,
    };

    rb_node_t* y = tree->nil;
    rb_node_t* x = tree->root;
    while (x != tree->nil) {
        y = x;
        if (tree->value(node) < tree->value(x))
            x = x->left;
        else
            x = x->right;
    }

    node->parent = y;
    if (y == tree->nil) {
        tree->root = node;
    } else if (tree->value(node) < tree->value(y)) {
        y->left = node;
    } else {
        y->right = node;
    }

    insert_fixup(tree, node);
}

rb_node_t* rb_find(rb_tree_t* tree, rb_value_t key) {
    rb_node_t* x = tree->root;
    while (x != tree->nil) {
        rb_value_t v = tree->value(x);
        if (key < v)
            x = x->left;
        else if (key > v)
            x = x->right;
        else
            return x;
    }

    return tree->nil;
}

static void delete_fixup(rb_tree_t* tree, rb_node_t* x) {
    while (x != tree->root && IS_BLACK(x)) {
        rb_node_t* p = x->parent;
        rb_node_t* w;

        if (x == p->left) {
            w = sibling(tree, x);

            if (IS_RED(w)) {
                w->color = BLACK;
                p->color = RED;
                rotate_left(tree, p);
                w = p->right;
            }

            if (IS_BLACK(w->left) && IS_BLACK(w->right)) {
                w->color = RED;
                x = p;
            } else {
                if (IS_BLACK(w->right)) {
                    w->left->color = BLACK;
                    w->color = RED;
                    rotate_right(tree, w);
                    w = p->right;
                }

                w->color = p->color;
                p->color = BLACK;
                w->right->color = BLACK;
                rotate_left(tree, p);
                x = tree->root;
            }
        } else {
            w = sibling(tree, x);

            if (IS_RED(w)) {
                w->color = BLACK;
                p->color = RED;
                rotate_right(tree, p);
                w = p->left;
            }

            if (IS_BLACK(w->left) && IS_BLACK(w->right)) {
                w->color = RED;
                x = p;
            } else {
                if (IS_BLACK(w->left)) {
                    w->right->color = BLACK;
                    w->color = RED;
                    rotate_left(tree, w);
                    w = p->left;
                }

                w->color = p->color;
                p->color = BLACK;
                w->left->color = BLACK;
                rotate_right(tree, p);
                x = tree->root;
            }
        }
    }

    x->color = BLACK;
}

void rb_delete(rb_tree_t* tree, rb_node_t* z) {
    ASSERT(z != tree->nil);
    ASSERT(rb_node_is_linked_in(tree, z));

    rb_node_t* y = z;
    bool y_original_color = y->color;
    rb_node_t* x;

    if (z->left == tree->nil) {
        x = z->right;
        transplant(tree, z, z->right);
    } else if (z->right == tree->nil) {
        x = z->left;
        transplant(tree, z, z->left);
    } else {
        y = rb_minimum(tree, z->right);
        y_original_color = y->color;
        x = y->right;

        if (y->parent == z) {
            x->parent = y;
        } else {
            transplant(tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }

        transplant(tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    tree->count--;

    if (y_original_color == BLACK) {
        delete_fixup(tree, x);
    }
}

rb_node_t* rb_lower_bound(rb_tree_t* tree, rb_value_t key) {
    rb_node_t* cur = tree->root;
    rb_node_t* res = tree->nil;
    while (cur != tree->nil) {
        rb_value_t v = tree->value(cur);
        if (v >= key) {
            res = cur;
            cur = cur->left;
        } else {
            cur = cur->right;
        }
    }
    return res;
}
