#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t* data;
    size_t capacity;
    size_t head, tail;
} RingBuffer;

RingBuffer* ringbuf_new(size_t capacity);
void ringbuf_free(RingBuffer* buf);
bool ringbuf_empty(const RingBuffer* buf);
bool ringbuf_full(const RingBuffer* buf);
bool ringbuf_push(RingBuffer* buf, uint8_t byte);
bool ringbuf_pop(RingBuffer* buf, uint8_t* out);
