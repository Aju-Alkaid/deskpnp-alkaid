#ifndef __APP_TEST_H
#define __APP_TEST_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "driver_motor.h"
#include "cmsis_os2.h"

void StartUartTestTask(void *argument);
void PrintDebug(const char* fmt, ...);

typedef struct {
    int32_t x_axis;   // X轴绝对坐标
    int32_t y_axis;   // Y轴绝对坐标
} tMotionCmd_t;

// 测试任务句柄
void StartDefaultTask(void *argument);   // 由CubeMX生成的任务入口
void vMotorTestTask(void *pvParameters); // 测试任务

void StartHostCommTestTask(void *argument); // 上位机通讯测试任务
void StartHostMotionTestTask(void *argument);
extern const osThreadAttr_t hostMotionTestTask_attributes;

#endif
