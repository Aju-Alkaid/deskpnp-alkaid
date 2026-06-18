#ifndef __APP_HOST_H
#define __APP_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os2.h"
#include "app_uart_parser.h"
#include "app_vision.h"
#include "app_motion.h"
#include "app_config.h"     /* CalibrationData_t, STEPS_PER_MM */

/* 单板最大元件数 */
#define MAX_COMPONENTS  128
#define MAX_MARKS       8     /* Mark 点最大数量（规范为 5） */

/* 下载超时(ms) */
#define DOWNLOAD_TIMEOUT_MS  500

/* R 轴转速 */
#define R_SPEED_RPM         60.0f

/* 元件信息 */
typedef struct {
    uint16_t id;
    float target_x;
    float target_y;
    float target_angle;
    char     footprint[32];   /* 封装名称 (C0805, R0805, LED-SMD, Mark1~5) */
    char     layer;           /* 层面 'T' 或 'B' */
    bool     is_mark;         /* SMD=="MARK" 时为 true */
    uint8_t feeder_id;
    bool placed;
} Component_t;

/* ---- 统一 Host 任务状态 ---- */
typedef enum {
    HOST_INIT,           /* 启动，发送 DEBUG_MODE */
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

#endif