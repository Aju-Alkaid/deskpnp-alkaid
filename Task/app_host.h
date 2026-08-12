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

/* PnP 运动速度/加速度常量 */
#define PNP_SPEED        400   /* 通用速度 (RPM) */
#define PNP_ACC          40    /* 通用加速度 */
#define PNP_SPEED_FINE   100   /* 视觉迭代微调 */
#define P1_SCAN_SPEED     100   /* P1 扫描移动速度 (RPM) */
#define PNP_ACC_FINE     10    /* 微调加速度 */

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
    HOST_P4_BASELINE,    /* P4: 建系前下相机基线校验 */
    HOST_P4_VERIFY,      /* P4: 建系后下相机漂移校验 */
    HOST_FIND_COMP,      /* P1: 散料区找元件 */
    HOST_PICK,              /* 吸取元件 (Z轴+气泵) */
    HOST_REPICK,            /* P3 空吸嘴: 返回取料点重新吸取 */
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

/* ---- PNPSTOP/PCONTINUE 断点恢复上下文 ---- */
#define RESUME_COORD_TOL_STEPS 250

typedef enum {
    RESUME_STEP_NONE = 0,
    RESUME_STEP_MARK_ALIGN,
    RESUME_STEP_P4_BASELINE,
    RESUME_STEP_P4_VERIFY,
    RESUME_STEP_FIND_COMP,
    RESUME_STEP_PICK,
    RESUME_STEP_MOVE_BOTTOM_CAM,
    RESUME_STEP_OFFSET_CHECK,
    RESUME_STEP_MOVE_TO_PCB,
    RESUME_STEP_PLACE,
} ResumeStepId_t;

typedef struct {
    uint16_t task_id;
    ResumeStepId_t step_id;
    int32_t  coord_x_steps;
    int32_t  coord_y_steps;
    float    coord_r_deg;
    float    coord_z_deg;
    uint16_t comp_index;
    uint16_t comp_count;
    bool     placed_flag;
    bool     coord_synced;
    uint32_t saved_tick;
} ResumeContext_t;

/* 外部接口 */
extern osMessageQueueId_t host_pkt_queue;

extern PCBFrame_t g_pcb_frame;
extern ResumeContext_t g_resume_ctx;

/* 散料区子扫描位: [cell][subpos][x/y] */
extern int32_t g_scatter_subpos[4][5][2];
void scatter_init_cells(void);
bool host_start_r_correction(const VisionResult_t *r, const char *stage);

void Host_ImportCsvLine(const char *line, uint16_t len);
void Host_FinishCsvImport(void);
void Host_CsvImportAbort(void);
uint8_t Host_IsSmtFinished(void);

void Host_UartRecvCallback(uint8_t *data, int len);
void Host_Task(void *argument);

#endif
