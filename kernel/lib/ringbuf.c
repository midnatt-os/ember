#include "lib/ringbuf.h"
#include "memory/heap.h"



RingBuffer* ringbuf_new(size_t capacity) {
    RingBuffer* buf = kmalloc(sizeof(RingBuffer));
    *buf = (RingBuffer) {
        .data = kmalloc(sizeof(uint64_t) * capacity),
        .capacity = capacity,
        .head = 0,
        .tail = 0
    };

    return buf;
}

bool ringbuf_empty(const RingBuffer* buf) {
    return buf->head == buf->tail;
}

bool ringbuf_full(const RingBuffer* buf) {
    return ((buf->head + 1) % buf->capacity) == buf->tail;
}

bool ringbuf_push(RingBuffer* buf, uint64_t value) {
    if (ringbuf_full(buf)) return false;

    buf->data[buf->head] = value;
    buf->head = (buf->head + 1) % buf->capacity;
    return true;
}

bool ringbuf_pop(RingBuffer* buf, uint64_t* out) {
    if (ringbuf_empty(buf)) return false;

    *out = buf->data[buf->tail];
    buf->tail = (buf->tail + 1) % buf->capacity;
    return true;
}

void ringbuf_free(RingBuffer* buf) {
    if (buf == nullptr) return;
    kfree(buf->data);
    kfree(buf);
}
