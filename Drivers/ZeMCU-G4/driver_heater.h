#ifndef __DRIVER_HEATER_H
#define __DRIVER_HEATER_H

#include <stdint.h>
#include "cmsis_os.h"

/* ========== CAN ID 定义（加热台从机协议） ========== */
#define HEATER_CMD_ID       0x04   /* 主控 → 加热台 命令帧 */
#define HEATER_STATUS_ID    0x05   /* 加热台 → 主控 状态帧 */

/* ========== 命令码 ========== */
#define HEATER_CMD_START      0x01  /* 启动回流焊曲线 */
#define HEATER_CMD_STOP       0x02  /* 停止加热 */
#define HEATER_CMD_SET_TEMP   0x03  /* 手动模式：设定目标温度（带 CRC） */
#define HEATER_CMD_SET_PID    0x04  /* 设定 PID 参数（带 CRC） */
#define HEATER_CMD_QUERY      0x05  /* 查询当前状态（单次回复） */

/* ========== 状态码（加热台 → 主控，字节0） ========== */
#define HEATER_STATE_IDLE       0x00  /* 空闲，加热关闭 */
#define HEATER_STATE_HEATING    0x01  /* 加热中（曲线/手动） */
#define HEATER_STATE_HOLDING    0x02  /* 恒温保持 */
#define HEATER_STATE_COOLING    0x03  /* 降温中，仍在回报 */
#define HEATER_STATE_COMPLETE   0x04  /* 曲线完成，加热关闭 */
#define HEATER_STATE_ERROR      0x05  /* 故障（查看 error 字节） */

/* ========== 错误码 ========== */
#define HEATER_ERR_NONE             0x00  /* 无错误 */
#define HEATER_ERR_THERMOCOUPLE     0x01  /* 热电偶断开 */
#define HEATER_ERR_OVERTEMP         0x02  /* 超温 */
#define HEATER_ERR_COMM_TIMEOUT     0x03  /* 通信超时 */

/* ========== 加热台状态结构体 ========== */
typedef struct {
    uint8_t  state;        /* 当前状态（见 HEATER_STATE_*） */
    int16_t  cur_temp;     /* 当前温度，单位 0.1°C，有符号 16 位 */
    int16_t  tar_temp;     /* 目标温度，单位 0.1°C，有符号 16 位 */
    uint8_t  error;        /* 错误码（见 HEATER_ERR_*） */
    uint32_t timestamp;    /* 最后更新时间 (HAL tick) */
} HeaterStatus_t;

/* ========== 函数声明 ========== */
void Heater_Init(void);
void Heater_SendStart(void);
void Heater_SendStop(void);
void Heater_SendQuery(void);
void Heater_SetTemperature(int16_t temp_0_1c);         /* 手动设定温度，单位 0.1°C */
void Heater_SetPID(int16_t Kp, int16_t Ki, int16_t Kd); /* 设定 PID，单位 0.001 */
void Heater_ProcessStatus(void);                       /* 任务中调用，处理接收到的状态帧 */
HeaterStatus_t Heater_GetCurrentStatus(void);          /* 获取最新状态快照 */

/* ========== 外部队列（CAN ISR 写入，任务中读取） ========== */
extern osMessageQueueId_t heater_rx_queue;

#endif
