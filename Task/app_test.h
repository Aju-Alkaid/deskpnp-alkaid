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


/*信号量*/
extern osSemaphoreId_t semX1Done, semX2Done, semYDone;//三轴独立信号量
extern osSemaphoreId_t semEmergency;//紧停信号量

/*事件组*/
extern osEventFlagsId_t evtAxesDone;   // 用于三轴到位同步

#endif
