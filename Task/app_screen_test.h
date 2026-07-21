#ifndef __APP_SCREEN_TEST_H
#define __APP_SCREEN_TEST_H

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include <stdbool.h>

/* ---- 屏幕功能测试任务 ---- */
void StartScreenTestTask(void *argument);
extern const osThreadAttr_t screenTestTask_attributes;

/* 供 app_host.c 调试命令调用的入口 */
void ScreenTest_Run(void);

#endif