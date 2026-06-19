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

/* 元件信息 */
typedef struct {
    uint16_t id;
    float target_x;
    float target_y;
    float target_angle;
    char     footprint[48];   /* 封装名称 (C0805, R0805, LED-SMD, Mark1~5) */
    char     layer;           /* 层面 'T' 或 'B' */
    bool     is_mark;         /* SMD=="MARK" 时为 true */
    uint8_t feeder_id;
    bool placed;
} Component_t;

/* ---- Mark 点 (P2 建系) ---- */
typedef struct {
    float    theory_x_mm;       /* Mark 理论坐标 (相对 PCB 原点) */
    float    theory_y_mm;
    bool     done;
    int32_t  actual_x_steps;    /* 对齐后在机器坐标中的位置 (步数) */
    int32_t  actual_y_steps;
} MarkPoint_t;

/* ---- PCB 坐标系 (由 P2 建系计算) ---- */
typedef struct {
    int32_t  origin_x_steps;    /* PCB 原点在机器坐标中的位置 */
    int32_t  origin_y_steps;
    float    rotation_rad;      /* PCB 放置旋转角 (弧度) */
    bool     valid;             /* 建系是否有效 (Mark3 验证通过) */
} PCBFrame_t;

/* ---- 散料区单元格 ---- */
#define SCATTER_CELLS   4
#define SCATTER_SUBPOS  5   /* 每格 5 个子扫描位: 中心+四角 */
typedef struct {
    int32_t center_x_steps;
    int32_t center_y_steps;
} ScatterCell_t;

/* ---- 统一 Host 任务状态 ---- */
typedef enum {
    HOST_HOME,              /* 等待手动归零 (SET_ORIGIN) */
    HOST_DEBUG,          /* 调试模式：手动电机控制 */
    HOST_DOWNLOADING,    /* 接收 CSV 文件 */
    HOST_MARK_ALIGN,     /* P2: Mark 点建系 */
    HOST_FIND_COMP,      /* P1: 散料区找元件 */
    HOST_PICK,              /* 吸取元件 (Z轴+气泵) */
    HOST_MOVE_TO_BOTTOM_CAM,/* 移动到下相机 */
    HOST_OFFSET_CHECK,      /* P3: 下相机偏移检测 */
    HOST_MOVE_TO_PCB,       /* 计算目标坐标 + 移动到贴装位 */
    HOST_PLACE,             /* 贴装元件 */
    HOST_DONE,           /* 全部完成 */
    HOST_REFLOW,            /* 回流焊进行中，等待加热台完成 */
    HOST_ERROR,             /* 错误 */
    HOST_WAIT_REFILL,       /* 散料区空，等待补料 */
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

extern PCBFrame_t g_pcb_frame;

void Host_UartRecvCallback(uint8_t *data, int len);
void Host_Task(void *argument);

#endif