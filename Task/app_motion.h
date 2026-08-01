#ifndef __APP_MOTION_H
#define __APP_MOTION_H

#include "cmsis_os2.h"
#include <stdint.h>
#include <stdbool.h>

#define R_AXIS_ADDR   0x04   // 虚拟地址，用于日志记录和调试，实际 R 轴控制可能复用 X/Y 的某个地址

/* ---- 机器座标系 (单例，线程安全) ---- */
typedef struct {
    int32_t x;           /* X1/X2 同步轴坐标 (步数) */
    int32_t y;           /* Y 轴坐标 (步数) */
    float   r;           /* R 轴角度 (deg, 0~360) */
    float   z;           /* Z 轴角度 (deg) */
    bool    homed;       /* 是否已完成归零 */
    bool    valid;       /* 座标是否可信 (false = 可能失步/未归零) */
} MachineCoord_t;

/* 座标核心接口 */
void Coord_Init(void);
MachineCoord_t Coord_Get(void);
void Coord_SetHome(void);
void Coord_UpdateXY(int32_t x, int32_t y);
void Coord_UpdateR(float angle);
void Coord_UpdateZ(float angle);
void Coord_Invalidate(void);

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


/* ---- Z轴+吸嘴+R轴 动作（供 Host_Task 直接调用）---- */
void nozzle_on(void);
void nozzle_off(void);
void z_down(void);
void z_up(void);
void z_safe(void);
void z_pick(void);
void z_place(void);
bool pick_component(void);
void place_component(void);
bool vacuum_ok(void);  /* __weak stub, override with GPIO/ADC */
void r_axis_set_zero(void);
float r_axis_calibrate(void);
int  r_axis_rotate(float angle, float speed_rpm);  /* 阻塞式旋转, 返回0=成功 */

/* ---- 兼容旧非阻塞 API (内部封装修为阻塞调用) ---- */
typedef enum { R_IDLE, R_BUSY, R_DONE, R_STALL, R_STUCK, R_TIMEOUT } R_State_t;
void      r_axis_start(float angle, float speed_rpm);  /* 内部调 r_axis_rotate, 阻塞返回 */
void      r_axis_poll(void);                            /* 空操作 */
R_State_t r_axis_state(void);                           /* 返回 R_DONE (阻塞式已执行完) */
int  safe_move_to(int32_t target_x, int32_t target_y, uint16_t speed, uint8_t acc);
void move_start_async(int32_t dx, int32_t dy, uint16_t speed, uint8_t acc);
void move_set_pad_ms(uint32_t pad_ms);

/* 电机异常分级 */
typedef enum {
    MOTOR_OK = 0,
    MOTOR_ERR_TIMEOUT,   /* 10s 内未到位，可重试 */
    MOTOR_ERR_LIMIT,     /* 限位触发或堵转，不可恢复 */
} MotorError_t;

extern volatile bool g_motor_error;
extern volatile MotorError_t g_motor_error_detail;
extern volatile int32_t g_enc_pos[4];    /* 31H encoder stash (ID 1-3) */
extern volatile bool    g_enc_ready[4];
extern volatile uint32_t g_axes_done_bits;  /* CAN ISR 鈫?运动到位标志 */
extern volatile bool     g_axes_error;      /* CAN ISR 鈫?堵转/限位标志 */

/* ---- XY 运动控制（从 app_test.c 迁移）---- */
void axis_stop(int32_t addr);
void disable_sync_stop(void);
int  move_xy_relative(int32_t dx, int32_t dy, uint16_t speed, uint8_t acc);
int  motion_read_encoder(uint8_t id, int32_t *out, uint32_t timeout_ms);
int  jog_stop_update_coord(const char *dir_name, int32_t enc_x1_start, int32_t enc_x2_start, int32_t enc_y_start);

/* ---- P2 连续扫描运动控制 ---- */
void p2_scan_start(uint8_t dir, uint16_t speed, uint8_t acc);
void p2_scan_stop(void);
int32_t p2_scan_estimate_x(int32_t start_x, int32_t sign, uint16_t speed, uint32_t elapsed_ms);
void p2_scan_step_y(int32_t dy_steps, uint16_t speed, uint8_t acc);
int32_t p2_stop_and_read_pos(int32_t enc_start_x1, int32_t enc_start_x2, int32_t coord_start_x);

extern volatile bool s_cmd_interrupted;

#endif
