#ifndef DATA_TRANSFER_H
#define DATA_TRANSFER_H

#include <stdint.h>
#include "key.h"
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- 数据类型枚举 ----
typedef enum {
    DT_SMT_STATUS = 0,      // SMT贴片状态变更
    DT_TEMP_CHANGE,         // 加热台温度变更
    DT_DOWNLOAD_STATUS,     // 下载/导入状态变更
    DT_SMT_PROGRESS,        // 贴片进度变更
    DT_MOTOR_RESET_DONE,    // 电机复位完成
    DT_KEY_EVENT,           // 按键事件（备用）
    DT_CUSTOM_MSG,          // 自定义消息
} DataTransferType_t;

// ---- 数据传输消息体 ----
typedef struct {
    DataTransferType_t type;
    union {
        uint8_t  u8_val;
        uint16_t u16_val;
        int32_t  i32_val;
        uint8_t  raw[8];
    } data;
} DataTransferMsg_t;

// ---- 全局状态变量（向后兼容，直接读写）----
extern uint8_t  if_now_SMT;
extern uint8_t  total_SMT;
extern uint8_t  now_SMT;
extern uint16_t Temp;
extern uint8_t  if_DOWNLOAD_READY;

// ---- 外部函数接口（空壳，待主程序实现）----
void motorReset_Start(void);
int  motorReset_IsDone(void);
void smt_Start(void);

// ---- 队列句柄（由 app_freertos.c 初始化）----
extern osMessageQueueId_t dataTransferQueue;

// ---- 发送 API（供主系统任务调用）----
void DT_SendSMTStatus(uint8_t is_smt);
void DT_SendTemp(uint16_t temp);
void DT_SendDownloadStatus(uint8_t status);
void DT_SendSMTProgress(uint8_t current, uint8_t total);
void DT_SendMotorResetDone(void);
void DT_SendCustom(uint8_t code, uint8_t param);

#ifdef __cplusplus
}
#endif

#endif // DATA_TRANSFER_H