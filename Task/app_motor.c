#include "app_motor.h"
#include "driver_motor.h" 
#include <stdio.h>

#define PULSES_PER_MM 512.0f 
#define MOTOR_X1_ID     0x01 // 确保这些 ID 定义与 CAN ID 一致
#define MOTOR_X2_ID     0x02
#define MOTOR_Y_ID      0x03

static MotionState_t g_motion_state = MOTION_STATE_IDLE;
static int32_t g_target_x_pulse = 0;
static int32_t g_target_y_pulse = 0;

static volatile bool g_axis_x1_done = false;
static volatile bool g_axis_x2_done = false;
static volatile bool g_axis_y_done = false;

static int32_t g_motor_x_position = 0;
static int32_t g_motor_y_position = 0;
static uint32_t g_motion_start_time = 0;

// 底层发送函数封装 (修正返回值问题)
// 假设 positionMode3Run 在你的 driver_motor.c 中是 void，我们这里包一层
static uint8_t Send_Move_Command(uint8_t id, int32_t pulses)
{
    // 调用你原本的 void 函数
    positionMode3Run(id, 300, 100, pulses); 
    return 0; // 假设发送总是成功，或者你可以检查 CAN 发送寄存器的状态
}


/**
 * @brief 专门用于处理 CAN 回调的函数 (需在 main 或 CAN 驱动中注册调用)
 * 当收到电机反馈时调用此函数
 */
void PnP_Motor_RX_Handler(CAN_Rx_Packet_t *pkt) {
    // 直接放入队列（在中断中安全，超时=0）
    osMessageQueuePut(motor_event_queue, pkt, 0, 0);
}



/**
  * @brief  运动控制主任务 (需要在 FreeRTOS 中创建)
  */
void PnP_Motion_Task(void *argument)
{
    CAN_Rx_Packet_t rx_frame; // 复用 CAN 数据包结构
    int32_t target_x_pulse = 0;
    int32_t target_y_pulse = 0;    
    for(;;)
    {
        switch (g_motion_state)
        {
            case MOTION_STATE_IDLE:
                osDelay(10); 
                break;

            case MOTION_STATE_MOVING:
                g_axis_x1_done = false;
                g_axis_x2_done = false;
                g_axis_y_done = false;
                g_motion_start_time = HAL_GetTick();
                // 添加xy轴的运动指令
                // positionMode3Run 定义在 driver_motor.c，发送 0xF5 指令
                positionMode3Run(MOTOR_X1_ID, 300, 100, target_x_pulse);
                positionMode3Run(MOTOR_X2_ID, 300, 100, target_x_pulse);
                positionMode3Run(MOTOR_Y_ID, 300, 100, target_y_pulse);						
                // 2. 记录目标值 (用于后续比较)
                // target_x_pulse 和 target_y_pulse 需要从上一级传入或作为全局变量
                motorSyncTrigger(0); // 广播0x48 同步启动
                // 3. 立即进入 WAITING 状态
                g_motion_state = MOTION_STATE_WAITING;
                break;					

            case MOTION_STATE_WAITING:

                // 尝试从队列获取消息
                // 类型是 CAN_Rx_Packet_t
                 
                if (osMessageQueueGet(motor_event_queue, &rx_frame, NULL, 0) == osOK)
                {

                    if (rx_frame.Data[0] == 0xF5 && rx_frame.Data[1] == 0x02) {
                    // 判断电机 ID
                    if (rx_frame.ID == MOTOR_X1_ID)      g_axis_x1_done = true;
                    else if (rx_frame.ID == MOTOR_X2_ID) g_axis_x2_done = true;
                    else if (rx_frame.ID == MOTOR_Y_ID)  g_axis_y_done  = true;
                    }
                }       

                if (g_axis_x1_done && g_axis_x2_done && g_axis_y_done)
                {
                    printf("Motion Complete!\r\n");
                    g_motion_state = MOTION_STATE_COMPLETED;
                }

                // 超时保护
                if (HAL_GetTick() - g_motion_start_time > 10000)
                {
                    printf("Motion Timeout!\r\n");
                    g_motion_state = MOTION_STATE_IDLE;
                }
								osDelay(10); // 避免 CPU 占用过高
                break;

            case MOTION_STATE_COMPLETED:
                g_motion_state = MOTION_STATE_IDLE;
                break;
			case MOTION_STATE_ERROR:
				printf("Motion Error!\r\n");
				break;
        }
        osDelay(1); 
    }
}

uint8_t PnP_MoveTo(float x_mm, float y_mm)
{
    if (g_motion_state != MOTION_STATE_IDLE) return 1;

    int32_t target_x = (int32_t)(x_mm * PULSES_PER_MM);
    int32_t target_y = (int32_t)(y_mm * PULSES_PER_MM);
	//发送标志位
	g_axis_x1_done = false;
    g_axis_x2_done = false;
	g_axis_y_done = false;
	g_motion_start_time = HAL_GetTick();

    positionMode3Run(MOTOR_X1_ID, 300, 100, target_x);
    positionMode3Run(MOTOR_X2_ID, 300, 100, target_x);
    positionMode3Run(MOTOR_Y_ID, 300, 100, target_y);

    // 广播同步执行
    motorSyncTrigger();	

    g_motion_state = MOTION_STATE_MOVING;
	return 0;
}



