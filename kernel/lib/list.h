#pragma once

#include <stddef.h>

#define LIST_NEW (list_t){ .head = nullptr, .tail = nullptr, .count = 0 }

#define LIST_NEXT(NODE) ((NODE)->next)
#define LIST_PREV(NODE) ((NODE)->prev)

#define LIST_FOREACH(LIST, NODE) for (list_node_t * (NODE) = (LIST).head; (NODE) != nullptr; (NODE) = LIST_NEXT(NODE))

typedef struct list_node list_node_t;
struct list_node {
    list_node_t* next;
    list_node_t* prev;
};

typedef struct {
    list_node_t* head;
    list_node_t* tail;
    size_t count;
} list_t;

bool list_is_empty(const list_t* l);

void list_node_append(list_t* l, list_node_t* position, list_node_t* node);
void list_node_prepend(list_t* l, list_node_t* position, list_node_t* node);

void list_append(list_t* l, list_node_t* node);
void list_prepend(list_t* l, list_node_t* node);
void list_delete(list_t* l, list_node_t* node);
list_node_t* list_pop(list_t* l);
list_node_t* list_peek(const list_t* l);
