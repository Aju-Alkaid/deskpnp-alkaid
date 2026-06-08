#ifndef __APP_VISION_H
#define __APP_VISION_H

#include <stdint.h>
#include <stdbool.h>

/* Mark 点数量 (供 app_host.c 使用) */
#define P2_MARK_COUNT  3

/* ================================================================
 *  新协议类型 (MaixCAM 2026)
 * ================================================================ */

/* 视觉模块对外状态 */
typedef enum {
    VISION_IDLE = 0,     /* 空闲，无进行中的 Process */
    VISION_BUSY,         /* 等待 CAM 响应 */
    VISION_GOT_STOP,     /* 收到 "stp"，需停电机后调用 Vision_Go() */
    VISION_GOT_POS,      /* 收到位置数据，需移动后调用 Vision_Go() */
    VISION_DONE,         /* 收到 "ok"，流程完成 */
    VISION_ERROR,        /* 收到错误 */
} VisionState_t;

/* 命令类型 */
typedef enum {
    VCMD_P1,             /* YOLO 检测元件 + 迭代对齐 */
    VCMD_P2,             /* Mark 点建系 */
    VCMD_P3,             /* 下相机边缘检测 + 对齐 */
} VisionCmd_t;

/* 单次 Process 的结果数据 */
typedef struct {
    /* P1 / P3 共用 */
    int32_t dx, dy;           /* 偏移 (P1/P3: 像素; P2: mm*10000) */
    /* P1 Phase 1 独有 */
    int32_t angle_x100;       /* 角度 x 100 (例: 450 = 4.50deg) */
    char    class_name[8];    /* 类别: "crest"/"ccapt"/"cledy"/"cledo" */
    /* P2 独有 */
    int32_t mark_index;       /* 当前 Mark 序号 (0-based) */
    int32_t mark_count;       /* 总 Mark 数 */
} VisionResult_t;

/* ---- 新协议 API ---- */

void Vision_Init(void);

/* 启动 Process (非阻塞)，状态变为 VISION_BUSY */
void Vision_Start(VisionCmd_t cmd);

/* 发送 "go" (主机停稳/移动完成后调用)，状态变为 VISION_BUSY */
void Vision_Go(void);

/* 查询当前状态 */
VisionState_t Vision_GetState(void);

/* 获取结果 (VISION_DONE / VISION_GOT_POS 时有效) */
const VisionResult_t* Vision_GetResult(void);

/* 获取错误码字符串 (VISION_ERROR 时有效) */
const char* Vision_GetError(void);

/* UART 回调：逐字节喂入 (由 driver_uart ISR 路径调用，内部不做阻塞操作) */
void CamUart_RecvCallback(uint8_t *data, int len);


/* ================================================================
 *  旧协议类型 (保留以兼容 app_host.h / app_host.c)
 *  Host_Task 集成完成后可移除
 * ================================================================ */

typedef enum {
    CAM_NONE = 0,
    CAM_PROC1_OK,
    CAM_PROC1_ERR,
    CAM_PROC2_OK,
    CAM_PROC2_ERR,
    CAM_PROC3_OK,
    CAM_PROC3_ERR,
} CamResult_t;

typedef struct {
    CamResult_t result;
    int32_t x_offset;
    int32_t y_offset;
    int32_t comp_info;
    int32_t mark1_x, mark1_y;
    int32_t mark2_x, mark2_y;
} CamData_t;

typedef enum {
    CAM_CMD_PROC1,
    CAM_CMD_PROC2,
    CAM_CMD_PROC3,
} CamCmd_t;

/* 旧 API 包装 (映射到新 API，逐步废弃) */
void Vision_SendCmd(CamCmd_t cmd);

#endif