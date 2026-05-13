#ifndef __APP_HOST_H
#define __APP_HOST_H

#include <stdint.h>
#include "cmsis_os2.h"
#include <stdbool.h>

/* 单板最大元件数 */
#define MAX_COMPONENTS  64

/* 元件信息 */
typedef struct {
    uint16_t id;
    float target_x;     /* PCB上目标X坐标(mm) */
    float target_y;     /* PCB上目标Y坐标(mm) */
    float target_angle; /* PCB上目标角度(deg) */
    uint8_t feeder_id;  /* 供料器编号 */
    bool placed;        /* 是否已贴装 */
} Component_t;

/* 上位机通信任务状态 */
typedef enum {
    HOST_IDLE,          /* 空闲，等待就绪 */
    HOST_RECEIVING,     /* 正在接收板数据 */
    HOST_PROCESSING,    /* 正在贴装 */
    HOST_DONE           /* 整板完成 */
} HostState_t;

/* 外部接口 */
extern osMessageQueueId_t host_pkt_queue;

void Host_UartRecvCallback(uint8_t *data, int len);
void Host_Task(void *argument);

#endif