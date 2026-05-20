
#ifndef __RINGBUF_H
#define __RINGBUF_H
#include <stdint.h>
#include <stdbool.h>
#include "stm32g4xx_hal.h"

#define CAM_RING_SIZE  1024  // 摄像头环形缓冲区大小
#define HOST_RING_SIZE 4096  // 上位机环形缓冲区大小

typedef struct {
    uint8_t *buffer;
    uint16_t size;
    volatile uint16_t head;  // 写索引
    volatile uint16_t tail;  // 读索引
} RingBuf_t;

void RingBuf_Init(RingBuf_t *rb, uint8_t *buf, uint16_t size);
bool RingBuf_Write(RingBuf_t *rb, const uint8_t *data, uint16_t len);
bool RingBuf_Read(RingBuf_t *rb, uint8_t *out, uint16_t len);
uint16_t RingBuf_Available(RingBuf_t *rb);
#endif