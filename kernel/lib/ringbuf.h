#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t* data;
    size_t capacity;
    size_t head;
    size_t tail;
} ringbuf_t;

void ringbuf_init(ringbuf_t* rb, uint8_t* storage, size_t capacity);
void ringbuf_reset(ringbuf_t* rb);

bool ringbuf_empty(const ringbuf_t* rb);
bool ringbuf_full(const ringbuf_t* rb);

size_t ringbuf_readable(const ringbuf_t* rb);
size_t ringbuf_writable(const ringbuf_t* rb);

bool ringbuf_push(ringbuf_t* rb, uint8_t v);
bool ringbuf_pop(ringbuf_t* rb, uint8_t* out);
