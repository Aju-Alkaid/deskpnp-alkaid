#include "ringbuf.h"
#include <string.h>

void RingBuf_Init(RingBuf_t *rb, uint8_t *buf, uint16_t size) {
    rb->buffer = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
}

bool RingBuf_Write(RingBuf_t *rb, const uint8_t *data, uint16_t len) {
    if (len == 0 || len > rb->size) return false;

    uint16_t used = (rb->head - rb->tail + rb->size) % rb->size;
    if (len > rb->size - used) return false;

    uint16_t first_part = rb->size - rb->head;
    if (first_part >= len) {
        memcpy(&rb->buffer[rb->head], data, len);
    } else {
        memcpy(&rb->buffer[rb->head], data, first_part);
        memcpy(&rb->buffer[0], data + first_part, len - first_part);
    }
    rb->head = (rb->head + len) % rb->size;
    return true;
}

bool RingBuf_Read(RingBuf_t *rb, uint8_t *out, uint16_t len) {
    if (RingBuf_Available(rb) < len) return false;
    uint16_t first_part = rb->size - rb->tail;
    if (first_part >= len) {
        memcpy(out, &rb->buffer[rb->tail], len);
    } else {
        memcpy(out, &rb->buffer[rb->tail], first_part);
        memcpy(out + first_part, &rb->buffer[0], len - first_part);
    }
    rb->tail = (rb->tail + len) % rb->size;
    return true;
}

uint16_t RingBuf_Available(RingBuf_t *rb) {
    return (rb->head - rb->tail + rb->size) % rb->size;
}
