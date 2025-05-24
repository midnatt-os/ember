#pragma once

#include <stddef.h>

#define LIST_NEW (List) { .head = nullptr, .tail = nullptr, .count = 0 }

#define LIST_NEXT(NODE) ((NODE)->next)
#define LIST_PREV(NODE) ((NODE)->prev)

#define LIST_ELEMENT_OF(NODE, TYPE, MEMBER) ((TYPE*) ((uintptr_t) (NODE) - __builtin_offsetof(TYPE, MEMBER)))
#define LIST_FOREACH(LIST, NODE) for (ListNode* (NODE) = (LIST).head; (NODE) != nullptr; (NODE) = LIST_NEXT(NODE))

typedef struct ListNode {
    struct ListNode* next;
    struct ListNode* prev;
} ListNode;

typedef struct {
    ListNode* head;
    ListNode* tail;
    size_t count;
} List;

bool list_is_empty(List* list);

void list_node_append(List* list, ListNode* position, ListNode* node);
void list_node_prepend(List* list, ListNode* position, ListNode* node);

void list_append(List* list, ListNode* node);
void list_prepend(List* list, ListNode* node);
void list_delete(List* list, ListNode* node);
ListNode* list_pop(List* list);
