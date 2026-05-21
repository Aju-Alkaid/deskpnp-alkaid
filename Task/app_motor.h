#ifndef _APP_MOTOR_H
#define _APP_MOTOR_H

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "driver_can.h" 

// 1. 定义电机轴 ID (根据你的硬件连接修改)
#define MOTOR_X1_ID     0x01
#define MOTOR_X2_ID     0x02
#define MOTOR_Y_ID      0x03

// 2. 定义运动状态机状态
typedef enum {
    MOTION_STATE_IDLE = 0,      // 空闲
    MOTION_STATE_MOVING,     // 指令已触发（发送中）
    MOTION_STATE_ERROR,       // 电机有误
    MOTION_STATE_COMPLETED,      // 运动完成
		MOTION_STATE_WAITING
} MotionState_t;



// 4. 全局句柄声明 (在 app_freertos.c 中创建)
extern osMessageQueueId_t motor_event_queue;

// 5. 核心接口声明
void PnP_Motor_RX_Handler(CAN_Rx_Packet_t *pkt); // 需要在 main 或 CAN 驱动中注册调用
uint8_t PnP_MoveTo(float x_mm, float y_mm);
void PnP_Motion_Task(void *argument); // 需要在 FreeRTOS 中启动的任务

#endif