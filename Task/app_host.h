#ifndef __APP_HOST_H
#define __APP_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os2.h"
#include "app_uart_parser.h"
#include "app_vision.h"
#include "app_motion.h"

/* 单板最大元件数 */
#define MAX_COMPONENTS  128

/* 下载超时(ms) */
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

/* ---- 统一 Host 任务状态 ---- */
typedef enum {
    HOST_INIT,           /* 启动，发送 DOWNLOAD_READY */
    HOST_DEBUG,          /* 调试模式：手动电机控制 */
    HOST_DOWNLOADING,    /* 接收 CSV 文件 */
    HOST_MARK_ALIGN,     /* P2: Mark 点建系 */
    HOST_FIND_COMP,      /* P1: 散料区找元件 */
    HOST_PICK,           /* 吸取元件 (Z轴+气泵) */
    HOST_OFFSET_CHECK,   /* P3: 下相机偏移检测 */
    HOST_PLACE,          /* 贴装元件 */
    HOST_DONE,           /* 全部完成 */
    HOST_ERROR,          /* 错误 */
} HostState_t;

/* 队列消息类型 */
typedef enum {
    MSG_HOST_CMD,        /* 上位机命令 */
    MSG_NONE = 0
} HostMsgType_t;

/* 队列消息 */
typedef struct {
    HostMsgType_t type;
    HostParsed_t  host_cmd;
} HostMsg_t;

/* 外部接口 */
extern osMessageQueueId_t host_pkt_queue;

void Host_UartRecvCallback(uint8_t *data, int len);
void Host_Task(void *argument);


/* ---- 校准位置 (步数) — 需实测标定！---- */
#define BOTTOM_CAM_X_STEPS   0      /* TODO: 下相机站 X 坐标 */
#define BOTTOM_CAM_Y_STEPS   0      /* TODO: 下相机站 Y 坐标 */
#define FEEDER_AREA_X_STEPS  0      /* TODO: 散料区起始 X 坐标 */
#define FEEDER_AREA_Y_STEPS  0      /* TODO: 散料区起始 Y 坐标 */

/* R 轴转速 */
#define R_SPEED_RPM         60.0f
#endif
