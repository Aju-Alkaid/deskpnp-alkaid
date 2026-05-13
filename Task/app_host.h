#ifndef __APP_HOST_H
#define __APP_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os2.h"
#include "app_uart_parser.h"
#include "app_vision.h"

/* 单板最大元件数 */
#define MAX_COMPONENTS  128

/* 下载超时(ms): 无新行超过此时间则视为下载结束 */
#define DOWNLOAD_TIMEOUT_MS  300

/* 元件信息 */
typedef struct {
    uint16_t id;
    float target_x;
    float target_y;
    float target_angle;
    uint8_t feeder_id;
    bool placed;
} Component_t;

/* 上位机通信任务状态 */
typedef enum {
    HOST_IDLE,          /* 空闲 */
    HOST_DOWNLOADING,   /* 正在接收文件 */
    HOST_ALIGN,         /* Mark点对齐 */
    HOST_FIND_COMP,     /* 查找元件 */
    HOST_OFFSET,        /* 偏移检测 */
    HOST_DEBUG,         /* 调试模式 */
} HostState_t;

/* 队列消息类型标签 */
typedef enum {
    MSG_HOST_CMD,       /* 上位机命令 */
    MSG_CAM_DATA        /* 摄像头数据 */
} HostMsgType_t;

/* 队列消息 */
typedef struct {
    HostMsgType_t type;
    union {
        HostParsed_t host_cmd;
        CamData_t    cam_data;
    } data;
} HostMsg_t;

/* 外部接口 */
extern osMessageQueueId_t host_pkt_queue;

void Host_UartRecvCallback(uint8_t *data, int len);
void Host_Task(void *argument);

#endif