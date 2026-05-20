#ifndef __DRIVER_HEATER_H
#define __DRIVER_HEATER_H

#include <stdint.h>
#include "cmsis_os.h"

/* CAN ID 定义 */
#define HEATER_CMD_ID       0x10   // 主控→加热台 命令
#define HEATER_STATUS_ID    0x11   // 加热台→主控 状态

/* 命令码 */
#define HEATER_CMD_START      0x01  // 启动曲线
#define HEATER_CMD_STOP       0x02  // 停止加热
#define HEATER_CMD_SET_TEMP   0x03  // 手动模式目标温度
#define HEATER_CMD_SET_PID    0x04  // 设置 PID 参数
#define HEATER_CMD_QUERY      0x05  // 查询状态

/* 状态码（加热台→主控，字节0） */
#define HEATER_STATE_STANDBY   0x00
#define HEATER_STATE_PREHEAT   0x01
#define HEATER_STATE_SOAK      0x02
#define HEATER_STATE_REFLOW    0x03
#define HEATER_STATE_COOLING   0x04
#define HEATER_STATE_COMPLETE  0x05
#define HEATER_STATE_ERROR     0x06

/* 错误码 */
#define HEATER_ERR_NONE        0
#define HEATER_ERR_SENSOR      1
#define HEATER_ERR_OVERTEMP    2
#define HEATER_ERR_TIMEOUT     3

/* 加热台状态结构体 */
typedef struct {
    uint8_t  state;        // 当前阶段
    int16_t  cur_temp;     // 当前温度 (0.1°C)
    int16_t  tar_temp;     // 目标温度 (0.1°C)
    uint8_t  error;        // 错误码
    uint32_t timestamp;    // 最后更新时间
} HeaterStatus_t;

/* 手动模式命令 */
typedef struct {
    uint8_t cmd;
    int16_t target_temp;   // 0.1°C
} HeaterSetTempCmd_t;

/* PID 参数命令 */
typedef struct {
    uint8_t cmd;
    int16_t Kp;   // 放大100倍
    int16_t Ki;
    int16_t Kd;
} HeaterSetPIDCmd_t;

/* 函数声明 */
void Heater_Init(void);
void Heater_SendStart(void);
void Heater_SendStop(void);
void Heater_SetTemperature(int16_t temp_0_1c);   // 手动设置温度，单位0.1°C
void Heater_SetPID(int16_t Kp, int16_t Ki, int16_t Kd);
void Heater_ProcessStatus(void);   // 任务中调用，处理接收到的状态
HeaterStatus_t Heater_GetCurrentStatus(void); // 获取最新状态

/* 外部队列（中断中写入，任务中读取） */
extern osMessageQueueId_t heater_rx_queue;

#endif