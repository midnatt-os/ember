#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t* data;
    size_t capacity;
    size_t head, tail;
} RingBuffer;

RingBuffer* ringbuf_new(size_t capacity);
void ringbuf_free(RingBuffer* buf);
bool ringbuf_empty(const RingBuffer* buf);
bool ringbuf_full(const RingBuffer* buf);
bool ringbuf_push(RingBuffer* buf, uint64_t value);
bool ringbuf_pop(RingBuffer* buf, uint64_t* out);
