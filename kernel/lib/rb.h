#pragma once

#include <stddef.h>

typedef struct rb_node rb_node_t;
typedef size_t rb_value_t;

struct rb_node {
    rb_node_t* parent;
    rb_node_t* left;
    rb_node_t* right;
    bool color;
};

typedef struct {
    rb_node_t* root;
    rb_node_t* nil;
    rb_value_t (*value)(const rb_node_t* n);
    size_t count;
    rb_node_t nil_node;
} rb_tree_t;

void rb_tree_init(rb_tree_t* tree, rb_value_t (*value)(const rb_node_t* n));
void rb_insert(rb_tree_t* tree, rb_node_t* new_node);
rb_node_t* rb_find(rb_tree_t* tree, size_t key);
rb_node_t* rb_lower_bound(rb_tree_t* tree, rb_value_t key);
void rb_delete(rb_tree_t* tree, rb_node_t* node);
rb_node_t* rb_minimum(rb_tree_t* tree, rb_node_t* node);
rb_node_t* rb_maximum(rb_tree_t* tree, rb_node_t* node);
rb_node_t* rb_predecessor(rb_tree_t* tree, rb_node_t* node);
rb_node_t* rb_successor(rb_tree_t* tree, rb_node_t* node);
