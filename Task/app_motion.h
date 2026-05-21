#ifndef __APP_MOTION_H
#define __APP_MOTION_H

#include "cmsis_os2.h"
#include <stdint.h>

#define R_AXIS_ADDR   0x04   // 虚拟地址，用于日志记录和调试，实际 R 轴控制可能复用 X/Y 的某个地址

typedef enum {
    MOTION_CMD_MOVE_TO,      // XY 绝对坐标移动
    MOTION_CMD_HOME,         // XY 回零
    MOTION_CMD_STOP,         // 急停
    MOTION_CMD_DISABLE,      // 松轴
    MOTION_CMD_Z_DOWN,       // Z 轴下降到贴装高度
    MOTION_CMD_Z_UP,         // Z 轴上升到安全高度
    MOTION_CMD_PICK,         // 吸取元件 (Z下降→吸嘴开→Z上升)
    MOTION_CMD_PLACE,        // 放置元件 (Z下降→吸嘴关→Z上升)
    MOTION_CMD_R_ROTATE,     // R 轴旋转到指定角度 (使用绝对位置)
    MOTION_CMD_WAIT          // 延时 (单位 ms)
} MotionCmdType_t;

// 事件组位掩码定义
#define EVENT_X1_DONE    (1 << 0)
#define EVENT_X2_DONE    (1 << 1)
#define EVENT_Y_DONE     (1 << 2)
#define EVENT_ALL_AXES   (EVENT_X1_DONE | EVENT_X2_DONE | EVENT_Y_DONE)
#define EVENT_ANY_ERROR  (1 << 3)   // 限位或堵转

/* 一条运动命令 (坐标模式) */
typedef struct {
    MotionCmdType_t cmd_type;
    int32_t target_x;        // 用于 MOVE_TO
    int32_t target_y;
    int32_t target_r;        // R 轴绝对角度 (0.01° 或 脉冲)
    int32_t param2;          // 通用参数, 如 WAIT 的延时
    uint16_t speed;
    uint8_t  acc;
} MotionCmd_t;



void Event_Init(void);

/* 任务间队列 */
extern osMessageQueueId_t motion_cmd_queue;
extern osMessageQueueId_t motor_event_queue;   // 与 CAN 中断共用

/*事件组*/
extern osEventFlagsId_t evtAxesDone;

#endif
