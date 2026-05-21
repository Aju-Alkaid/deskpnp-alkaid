#ifndef DATA_TRANSFER_H
#define DATA_TRANSFER_H

/*
 * ==================== 分发中枢（Dispatcher）====================
 *
 * 职责：FreeRTOS 主系统 ←→ TouchGFX GUI 双向通信的唯一切入点。
 *
 * 方向1 — System → GUI（通知）：系统任务调用 DT_Notify* 系列函数
 *         → dataTransferQueue → Model::processQueue() → Presenter → View
 *
 * 方向2 — GUI → System（命令）：Presenter 通过 Model::sendCommand()
 *         → guiCmdQueue → DT_Dispatch() → 路由表 → 目标任务回调
 *
 * 添加新消息只需 3 步：
 *   1. 在 DT_MsgType 枚举中加 ID（区分方向命名）
 *   2. 在 s_routeTable 中注册路由（handler 或 queue）
 *   3. 实现 handler / 发送函数
 * ================================================================
 */

#include <stdint.h>
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

// ======================== 消息类型枚举 ========================
// 命名约定：DT_xxx = System→GUI 通知, DT_CMD_xxx = GUI→System 命令
typedef enum {
    // ---- System → GUI 通知（0x00~0x0F）----
    DT_SMT_STATUS       = 0x00, // SMT 贴片状态变更
    DT_TEMP_CHANGE      = 0x01, // 加热台温度变更
    DT_DOWNLOAD_STATUS  = 0x02, // 下载/导入状态变更
    DT_SMT_PROGRESS     = 0x03, // 贴片进度变更
    DT_MOTOR_RESET_DONE = 0x04, // 电机复位完成
    DT_CUSTOM_MSG       = 0x05, // 自定义消息（备用）

    // ---- GUI → System 命令（0x10~0x2F）----
    DT_CMD_MOTOR_MOVE   = 0x10, // 电机移动到目标坐标
    DT_CMD_MOTOR_STOP   = 0x11, // 电机急停
    DT_CMD_MOTOR_HOME   = 0x12, // 电机回零
    DT_CMD_SMT_START    = 0x13, // 启动贴片流程
    DT_CMD_SMT_PAUSE    = 0x14, // 暂停贴片流程
    DT_CMD_HEATER_SET   = 0x15, // 设置加热台温度
    DT_CMD_SYSTEM_RESET = 0x16, // 系统软复位
    DT_CMD_CUSTOM       = 0x1F, // 自定义命令
} DT_MsgType_t;

// ======================== 统一消息体 ========================
typedef struct {
    DT_MsgType_t type;          // 消息类型（必填）
    union {
        // 贴片进度
        struct { uint8_t current, total; } progress;
        // 电机移动（mm * 100，避免浮点）
        struct { int32_t x, y, r; } move;
        // 温度（0.1℃ 为单位）
        uint16_t temp;
        // 状态/标志（单字节）
        uint8_t  status;
        // 原始数据（通用通道）
        uint8_t  raw[8];
    } data;
} DT_Msg_t;

// ======================== 路由表条目 ========================
// handler 和 queue 至少填一个。两者都填时优先 handler。
typedef void (*DT_CmdHandler)(const DT_Msg_t *msg);

typedef struct {
    DT_MsgType_t   type;       // 匹配的消息类型
    DT_CmdHandler  handler;    // 直接回调（适合简单逻辑）
    osMessageQueueId_t queue;  // 放入目标队列（适合跨任务通信）
} DT_Route_t;

// ======================== 全局状态变量（向后兼容）====================
extern uint8_t  if_now_SMT;
extern uint8_t  total_SMT;
extern uint8_t  now_SMT;
extern uint16_t Temp;
extern uint8_t  if_DOWNLOAD_READY;

// ---- 外部函数接口（待主程序实现）----
void motorReset_Start(void);
int  motorReset_IsDone(void);
void smt_Start(void);

// ======================== 队列句柄 ========================
extern osMessageQueueId_t dataTransferQueue;  // System → GUI
extern osMessageQueueId_t guiCmdQueue;        // GUI → System（在 app_freertos.c 创建）

// ======================== 初始化 ========================
// 注册路由表（在 app_freertos.c 的 MX_FREERTOS_Init 末尾调用）
void DT_Init(void);

// ======================== System → GUI 发送 API ========================
void DT_NotifySMTStatus(uint8_t is_smt);
void DT_NotifyTemp(uint16_t temp);
void DT_NotifyDownloadStatus(uint8_t status);
void DT_NotifySMTProgress(uint8_t current, uint8_t total);
void DT_NotifyMotorResetDone(void);
void DT_NotifyCustom(uint8_t code, uint8_t param);

// ======================== GUI → System 命令 API ========================
// 由 Model::sendCommand() 调用，通常不需要手动调用
void DT_SendCommand(const DT_Msg_t *cmd);

// 路由分发（由 DT_DispatchTask 或轮询调用）
// 遍历路由表，找到匹配的 handler/queue 并执行/转发
void DT_Dispatch(const DT_Msg_t *cmd);

#ifdef __cplusplus
}
#endif

#endif