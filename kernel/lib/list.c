#include "lib/list.h"

#include "common/assert.h"

static inline bool list_node_is_linked_in(const list_t* l, const list_node_t* n) {
    return n && (n == l->head || n == l->tail || n->next != nullptr || n->prev != nullptr);
}

bool list_is_empty(const list_t* l) {
    return l->count == 0;
}

void list_node_append(list_t* l, list_node_t* position, list_node_t* node) {
    ASSERT(node != position);
    ASSERT(!list_node_is_linked_in(l, node));

    node->prev = position;
    node->next = position->next;

    if (position->next != nullptr)
        position->next->prev = node;

    position->next = node;

    if (l->tail == position)
        l->tail = node;

    l->count++;
}

void list_node_prepend(list_t* l, list_node_t* position, list_node_t* node) {
    ASSERT(node != position);
    ASSERT(!list_node_is_linked_in(l, node));

    node->next = position;
    node->prev = position->prev;

    if (position->prev != nullptr)
        position->prev->next = node;

    position->prev = node;

    if (l->head == position)
        l->head = node;

    l->count++;
}

void list_append(list_t* l, list_node_t* node) {
    ASSERT(!list_node_is_linked_in(l, node));

    if (list_is_empty(l)) {
        l->head = l->tail = node;
        node->next = node->prev = nullptr;
        l->count = 1;
        return;
    }

    list_node_append(l, l->tail, node);
}

void list_prepend(list_t* l, list_node_t* node) {
    ASSERT(!list_node_is_linked_in(l, node));

    if (list_is_empty(l)) {
        l->head = l->tail = node;
        node->next = node->prev = nullptr;
        l->count = 1;
        return;
    }

    list_node_prepend(l, l->head, node);
}

void list_delete(list_t* l, list_node_t* node) {
    ASSERT(l->count > 0);

    if (node->prev == nullptr)
        ASSERT(l->head == node);
    else
        ASSERT(node->prev->next == node);

    if (node->next == nullptr)
        ASSERT(l->tail == node);
    else
        ASSERT(node->next->prev == node);

    if (node->prev != nullptr)
        node->prev->next = node->next;
    else
        l->head = node->next;

    if (node->next != nullptr)
        node->next->prev = node->prev;
    else
        l->tail = node->prev;

    node->next = node->prev = nullptr;
    l->count--;
}

list_node_t* list_pop(list_t* l) {
    if (list_is_empty(l))
        return nullptr;

    list_node_t* n = l->head;
    list_delete(l, n);
    return n;
}

list_node_t* list_peek(const list_t* l) {
    ASSERT(l);
    return l->head;
}
