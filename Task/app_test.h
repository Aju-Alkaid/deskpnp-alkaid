#ifndef __APP_TEST_H
#define __APP_TEST_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "driver_motor.h"
#include "cmsis_os2.h"
#include <stdbool.h>

#include "app_config.h"   /* 共享运动常量 */

/* ---- 共享运动控制函数 (已迁移到 app_motion.c/h) ---- */
#include "app_motion.h"


/* ---- 原测试任务声明 ---- */
void StartUartTestTask(void *argument);
void PrintDebug(const char* fmt, ...);

typedef struct {
    int32_t x_axis;
    int32_t y_axis;
} tMotionCmd_t;

void StartDefaultTask(void *argument);
void vMotorTestTask(void *pvParameters);
void StartHostCommTestTask(void *argument);
void StartHostMotionTestTask(void *argument);
extern const osThreadAttr_t hostMotionTestTask_attributes;

/* ---- Z轴+吸嘴+R轴 联合测试任务 ---- */
void StartPickPlaceTestTask(void *argument);
extern const osThreadAttr_t pickPlaceTestTask_attributes;


/* ---- 摄像头通讯测试任务 ---- */
void StartCamTestTask(void *argument);
extern const osThreadAttr_t camTestTask_attributes;

/* ---- ESP32 通信测试任务 ---- */
void StartESPTestTask(void *argument);
extern const osThreadAttr_t espTestTask_attributes;

#endif
