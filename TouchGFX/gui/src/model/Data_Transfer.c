#include "gui/model/Data_Transfer.h"

// ---- 全局状态变量（向后兼容）----
uint8_t  if_now_SMT = 1;
uint8_t  total_SMT  = 30;
uint8_t  now_SMT    = 15;
uint16_t Temp       = 388;
uint8_t  if_DOWNLOAD_READY = 0;

static uint8_t motor_reset_done = 0;

void motorReset_Start(void)
{
    motor_reset_done = 0;
}

int motorReset_IsDone(void)
{
    if (motor_reset_done == 0) {
        motor_reset_done = 1;
        return 0;
    }
    return 1;
}

void smt_Start(void)
{
    // TODO: 启动贴片流程
}

// 队列句柄（在 app_freertos.c 的 MX_FREERTOS_Init 中创建）
osMessageQueueId_t dataTransferQueue = NULL;

// ---- 发送辅助函数 ----
static void DT_SendMsg(DataTransferMsg_t *msg)
{
    if (dataTransferQueue != NULL) {
        osMessageQueuePut(dataTransferQueue, msg, 0, 0);
    }
}

void DT_SendSMTStatus(uint8_t is_smt)
{
    DataTransferMsg_t msg = { .type = DT_SMT_STATUS, .data.u8_val = is_smt };
    DT_SendMsg(&msg);
}

void DT_SendTemp(uint16_t temp)
{
    DataTransferMsg_t msg = { .type = DT_TEMP_CHANGE, .data.u16_val = temp };
    DT_SendMsg(&msg);
}

void DT_SendDownloadStatus(uint8_t status)
{
    DataTransferMsg_t msg = { .type = DT_DOWNLOAD_STATUS, .data.u8_val = status };
    DT_SendMsg(&msg);
}

void DT_SendSMTProgress(uint8_t current, uint8_t total)
{
    DataTransferMsg_t msg = { .type = DT_SMT_PROGRESS };
    msg.data.raw[0] = current;
    msg.data.raw[1] = total;
    DT_SendMsg(&msg);
}

void DT_SendMotorResetDone(void)
{
    DataTransferMsg_t msg = { .type = DT_MOTOR_RESET_DONE };
    DT_SendMsg(&msg);
}

void DT_SendCustom(uint8_t code, uint8_t param)
{
    DataTransferMsg_t msg = { .type = DT_CUSTOM_MSG };
    msg.data.raw[0] = code;
    msg.data.raw[1] = param;
    DT_SendMsg(&msg);
}