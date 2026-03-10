#include "ringbuf.h"

#include "common/assert.h"

static inline size_t wrap_next(size_t i, size_t cap) {
    i++;
    if (i == cap)
        i = 0;
    return i;
}

/* Acquire/release so data is visible before head moves, and tail is visible before reuse. */
static inline size_t load_acquire(const size_t* p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline void store_release(size_t* p, size_t v) {
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

void ringbuf_init(ringbuf_t* rb, uint8_t* storage, size_t capacity) {
    ASSERT(rb);
    ASSERT(storage);
    ASSERT(capacity >= 2);

    rb->data = storage;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
}

void ringbuf_reset(ringbuf_t* rb) {
    ASSERT(rb);

    store_release(&rb->head, 0);
    store_release(&rb->tail, 0);
}

bool ringbuf_empty(const ringbuf_t* rb) {
    ASSERT(rb);
    size_t h = load_acquire(&rb->head);
    size_t t = load_acquire(&rb->tail);
    return h == t;
}

bool ringbuf_full(const ringbuf_t* rb) {
    ASSERT(rb);
    size_t h = load_acquire(&rb->head);
    size_t t = load_acquire(&rb->tail);
    size_t n = wrap_next(h, rb->capacity);
    return n == t;
}

size_t ringbuf_readable(const ringbuf_t* rb) {
    ASSERT(rb);
    size_t h = load_acquire(&rb->head);
    size_t t = load_acquire(&rb->tail);

    if (h >= t)
        return h - t;
    return rb->capacity - (t - h);
}

size_t ringbuf_writable(const ringbuf_t* rb) {
    ASSERT(rb);
    /* one slot intentionally unused */
    return (rb->capacity - 1) - ringbuf_readable(rb);
}

bool ringbuf_push(ringbuf_t* rb, uint8_t v) {
    ASSERT(rb);

    size_t h = load_acquire(&rb->head);
    size_t t = load_acquire(&rb->tail);

    size_t n = wrap_next(h, rb->capacity);
    if (n == t)
        return false; // full

    rb->data[h] = v;

    /* publish after store to data[] */
    store_release(&rb->head, n);
    return true;
}

bool ringbuf_pop(ringbuf_t* rb, uint8_t* out) {
    ASSERT(rb);
    ASSERT(out);

    size_t t = load_acquire(&rb->tail);
    size_t h = load_acquire(&rb->head);

    if (t == h)
        return false; // empty

    *out = rb->data[t];

    size_t n = wrap_next(t, rb->capacity);

    /* publish after reading data[] */
    store_release(&rb->tail, n);
    return true;
}
