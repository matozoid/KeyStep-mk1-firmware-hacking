/* Single-producer / single-consumer byte ring (one side may be an interrupt). Size: power of 2. */
#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>

#define RINGBUF_SIZE 256u

typedef struct {
    volatile uint16_t head, tail;
    uint8_t buf[RINGBUF_SIZE];
} ringbuf_t;

static inline void ringbuf_init(ringbuf_t *r) { r->head = r->tail = 0; }
static inline unsigned ringbuf_count(const ringbuf_t *r) { return (uint16_t)(r->head - r->tail); }
static inline unsigned ringbuf_space(const ringbuf_t *r) { return RINGBUF_SIZE - ringbuf_count(r); }

static inline int ringbuf_put(ringbuf_t *r, uint8_t b)
{
    if (ringbuf_count(r) >= RINGBUF_SIZE)
        return 0;
    r->buf[r->head & (RINGBUF_SIZE - 1u)] = b;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    r->head++;
    return 1;
}

static inline int ringbuf_get(ringbuf_t *r, uint8_t *b)
{
    if (r->head == r->tail)
        return 0;
    *b = r->buf[r->tail & (RINGBUF_SIZE - 1u)];
    __atomic_thread_fence(__ATOMIC_RELEASE);
    r->tail++;
    return 1;
}

#endif
