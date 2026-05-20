#include "driver_servo.h"
#include <math.h>
#include <string.h>

/* 内部舵机数组，记录每个通道的配置和状态 */
static Servo_HandleTypeDef servo_handles[SERVO_TIM_CHANNEL_MAX] = {0};

/* 通道映射表 */
static const uint32_t servo_channels[SERVO_TIM_CHANNEL_MAX] = {
    TIM_CHANNEL_1,
    TIM_CHANNEL_2,
    TIM_CHANNEL_3,
    TIM_CHANNEL_4
};
/* 定时器 ARR 值（自动重装载），由 Servo_Init 计算 */
static uint16_t timer_arr = 0;

/**
 * @brief  初始化舵机模块，计算 PWM 比较值范围并启动所有通道
 * @param  htim : 指向已经配置为 50Hz PWM 输出的定时器句柄
 * @note   必须在 CubeMX 中设置定时器为 PWM 模式，且频率为 50Hz
 *         即：PWM 频率 = 定时器时钟 / (PSC+1) / (ARR+1) = 50 Hz
 */


void Servo_Init(TIM_HandleTypeDef *htim)
{
    if (htim == NULL) return;

    timer_arr = htim->Init.Period;   
    float pulse_per_us = (float)(timer_arr + 1) / (SERVO_PWM_PERIOD_MS * 1000.0f);
    uint16_t min_cmp = (uint16_t)(SERVO_PWM_MIN_US * pulse_per_us);
    uint16_t max_cmp = (uint16_t)(SERVO_PWM_MAX_US * pulse_per_us);
    uint16_t mid_cmp = (min_cmp + max_cmp) / 2;

    /* 只初始化你实际用到的通道：索引 2 → TIM_CHANNEL_3 → PE8 */
    uint8_t idx = 2;  // 对应 TIM_CHANNEL_3
    servo_handles[idx].htim      = htim;
    servo_handles[idx].channel   = TIM_CHANNEL_3;
    servo_handles[idx].min_pulse = min_cmp;
    servo_handles[idx].max_pulse = max_cmp;
    servo_handles[idx].mid_pulse = mid_cmp;

    /* 只启动我们需要的通道 */
    if (HAL_TIM_PWM_Start(htim, TIM_CHANNEL_3) != HAL_OK) {
        // 启动失败可以在这里用 LED 闪灯或打印，但当前尽量不要用阻塞打印
        // 可以在外边打印调试信息
    }
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_3, mid_cmp);
}
/**
 * @brief  设置舵机角度（0 ~ 180°）
 * @param  ch   : 舵机通道号，0 ~ SERVO_TIM_CHANNEL_MAX-1
 * @param  angle: 目标角度，超出范围会被截断
 * @note   非阻塞，直接修改 PWM 比较值
 */
void Servo_SetAngle(uint8_t ch, float angle)
{
		if (ch >= SERVO_TIM_CHANNEL_MAX || servo_handles[ch].htim == NULL) return;
	
    /* 限制角度范围 */
    if (angle < 0.0f) angle = 0.0f;
    if (angle > SERVO_ANGLE_RANGE) angle = SERVO_ANGLE_RANGE;

    /* 线性映射到比较值 */
    float scale = (float)(servo_handles[ch].max_pulse - servo_handles[ch].min_pulse) / SERVO_ANGLE_RANGE;
    uint16_t pulse = (uint16_t)(servo_handles[ch].min_pulse + angle * scale);

    __HAL_TIM_SET_COMPARE(servo_handles[ch].htim, servo_handles[ch].channel, pulse);
}

/**
 * @brief  快速设置舵机为中位（90°）
 * @param  ch: 通道号
 */
void Servo_SetMid(uint8_t ch)
{
    if (ch >= SERVO_TIM_CHANNEL_MAX) return;
    __HAL_TIM_SET_COMPARE(servo_handles[ch].htim,
                          servo_handles[ch].channel,
                          servo_handles[ch].mid_pulse);
}

/**
 * @brief  停止指定通道的 PWM 输出（释放舵机，不再维持位置）
 * @param  ch: 通道号
 */
void Servo_Stop(uint8_t ch)
{
    if (ch >= SERVO_TIM_CHANNEL_MAX) return;
    HAL_TIM_PWM_Stop(servo_handles[ch].htim, servo_handles[ch].channel);
}

/**
 * @brief  返回当前通道的近似角度（根据当前 CCR 计算）
 * @param  ch: 通道号
 * @return 角度值（0~180°）
 */
float Servo_GetAngle(uint8_t ch)
{
    if (ch >= SERVO_TIM_CHANNEL_MAX) return -1.0f;

    uint16_t pulse = __HAL_TIM_GET_COMPARE(servo_handles[ch].htim, servo_handles[ch].channel);
    float scale = (float)(servo_handles[ch].max_pulse - servo_handles[ch].min_pulse) / SERVO_ANGLE_RANGE;
    float angle = (pulse - servo_handles[ch].min_pulse) / scale;
    if (angle < 0.0f) angle = 0.0f;
    if (angle > SERVO_ANGLE_RANGE) angle = SERVO_ANGLE_RANGE;
    return angle;
}