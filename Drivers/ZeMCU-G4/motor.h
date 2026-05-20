#ifndef __MOTOR_H
#define __MOTOR_H

#include "pid.h"
#include "driver_tmc2209.h"
#include "main.h" // 用于GPIO定义
#include <stdint.h>

// --- 电机与机械参数配置 ---
// 根据你的28步进电机和丝杆/皮带参数修改此处
#define MOTOR_FULL_STEPS       200      // 电机全步步数 (1.8度 -> 200)
#define GEAR_REDUCTION_RATIO   1.0f     // 减速比 (无减速箱填1)
#define PULSES_PER_MM          80.0f    // 脉冲当量 (例如: 16微步 * 200 / 360 * 皮带轮齿数)

// --- GPIO 定义 (需与CubeMX配置一致) ---
// 假设使用 TIM2_CH1 (PA0) 作为 STEP, PA1 作为 DIR
#define MOTOR_STEP_PORT        GPIOA
#define MOTOR_STEP_PIN         GPIO_PIN_0
#define MOTOR_DIR_PORT         GPIOA
#define MOTOR_DIR_PIN          GPIO_PIN_1

// --- 控制模式 ---
typedef enum {
    CTRL_MODE_POSITION,  // 位置模式
    CTRL_MODE_SPEED,     // 速度模式
    CTRL_MODE_HOMING     // 回零模式
} Ctrl_Mode_t;

// --- 电机控制块 ---
typedef struct {
    // 状态
    Ctrl_Mode_t mode;
    float target_value;    // 目标值 (位置模式:脉冲数, 速度模式:RPM)
    float current_pos;     // 当前位置 (软件计数器)
    
    // 算法
    PID_Controller_t pid;  // PID控制器实例
    
    // 硬件参数
    uint32_t max_freq;    // 最大频率限制 (Hz)
    uint32_t min_pulse_us; // 最小脉冲宽度 (TMC2209手册要求 > 1.9us)
    
} Motor_Ctrl_t;

// --- API 声明 ---
void MOTOR_Init(Motor_Ctrl_t *mtr);
void MOTOR_SetPosition(Motor_Ctrl_t *mtr, float position_mm);
void MOTOR_SetSpeed(Motor_Ctrl_t *mtr, float rpm);
void MOTOR_Update(Motor_Ctrl_t *mtr); // 通常在定时器中断或高优先级任务中调用

// --- 硬件接口 (需在 stm32xx_it.c 或 HAL_TIM_Callback 中实现) ---
void MOTOR_HardwareStep_Callback(uint32_t current_freq);
void MOTOR_SetDirection(bool cw);


#endif