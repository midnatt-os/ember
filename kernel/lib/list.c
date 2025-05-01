#include "lib/list.h"

#include "common/assert.h"

bool list_is_empty(List* list) { return list->count == 0; }

void list_node_append(List* list, ListNode* position, ListNode* node) {
    ASSERT(position != nullptr);
    ASSERT(node != nullptr);

    list->count++;

    node->prev = position;
    node->next = position->next;

    position->next = node;

    if (position == list->tail) {
        list->tail = node;
        return;
    }

    node->next->prev = node;
}

void list_node_prepend(List* list, ListNode* position, ListNode* node) {
    ASSERT(position != nullptr);
    ASSERT(node != nullptr);

    list->count++;

    node->next = position;
    node->prev = position->prev;

    position->prev = node;

    if (position == list->head) {
        list->head = node;
        return;
    }

    node->prev->next = node;
}

void list_append(List* list, ListNode* node) {
    if (list_is_empty(list)) {
        list->head = list->tail = node;
        node->next = node->prev = nullptr;
        list->count++;

        return;
    }

    list_node_append(list, list->tail, node);
}

void list_prepend(List* list, ListNode* node) {
    if (list_is_empty(list)) {
        list->head = list->tail = node;
        node->next = node->prev = nullptr;
        list->count++;

        return;
    }

    list_node_prepend(list, list->head, node);
}

void list_delete(List* list, ListNode* node) {
    ASSERT(node != nullptr);

    if (node == list->head)
        list->head = node->next;

    if (node == list->tail)
        list->tail = node->prev;

    if (node->prev != nullptr)
        node->prev->next = node->next;

    if (node->next != nullptr)
        node->next->prev = node->prev;

    node->next = node->prev = nullptr;
    list->count--;
}

ListNode* list_pop(List* list) {
    ASSERT(!list_is_empty(list));

    ListNode* node = list->head;
    list_delete(list, node);
    return node;
}
