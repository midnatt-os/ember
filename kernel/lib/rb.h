#pragma once

#include <stddef.h>

typedef struct rb_node rb_node_t;
typedef size_t rb_value_t;

extern rb_node_t NIL_NODE;
#define RB_NEW(VALUE_FN) ((rb_tree_t) { .value = VALUE_FN, .nil = &NIL_NODE, .root = &NIL_NODE })

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
} rb_tree_t;

void rb_insert(rb_tree_t* tree, rb_node_t* new_node);
rb_node_t* rb_find(rb_tree_t* tree, size_t key);
rb_node_t* rb_lower_bound(rb_tree_t* tree, rb_value_t key);
void rb_delete(rb_tree_t* tree, rb_node_t* node);
rb_node_t* rb_minimum(rb_tree_t* tree, rb_node_t* node);
rb_node_t* rb_maximum(rb_tree_t* tree, rb_node_t* node);
rb_node_t* rb_predecessor(rb_tree_t* tree, rb_node_t* node);
rb_node_t* rb_successor(rb_tree_t* tree, rb_node_t* node);
