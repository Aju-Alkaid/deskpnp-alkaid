#include <stdbool.h>
#include "driver_servo.h"
#include <math.h>
#include <string.h>

/* ---- 诊断宏（由头文件 SERVO_DEBUG 开关控制） ---- */
#ifdef SERVO_DEBUG
extern void PrintDebug(const char* fmt, ...);
#define SERVO_LOG(fmt, ...)  PrintDebug("[Servo] " fmt, ##__VA_ARGS__)
#else
#define SERVO_LOG(fmt, ...)  ((void)0)
#endif

/* ================================================================
 *  模块内部状态
 * ================================================================ */

static Servo_HandleTypeDef servo_handles[SERVO_TIM_CHANNEL_MAX] = {0};
static uint16_t              timer_arr = 0;  /* ARR 缓存，避免重复读 */

/* ================================================================
 *  Servo_Init
 * ================================================================ */

void Servo_Init(TIM_HandleTypeDef *htim)
{
    if (htim == NULL) return;

    /* ---- 计算 0°/90°/180° 对应的 CCR ---- */
    timer_arr = htim->Init.Period;
    float pulse_per_us = (float)(timer_arr + 1)
                       / (SERVO_PWM_PERIOD_MS * 1000.0f);
    uint16_t min_cmp = (uint16_t)(SERVO_PWM_MIN_US * pulse_per_us);
    uint16_t max_cmp = (uint16_t)(SERVO_PWM_MAX_US * pulse_per_us);
    uint16_t mid_cmp = (min_cmp + max_cmp) / 2;

    /* ---- 通道 2 = TIM_CHANNEL_3（TIM2→PB10 或 TIM5→PE8） ---- */
    uint8_t idx = 2;
    servo_handles[idx].htim        = htim;
    servo_handles[idx].channel     = TIM_CHANNEL_3;
    servo_handles[idx].min_pulse   = min_cmp;
    servo_handles[idx].max_pulse   = max_cmp;
    servo_handles[idx].mid_pulse   = mid_cmp;
    servo_handles[idx].initialized = false;

    /* ---- GPIO 初始化（仅 TIM5_CH3，TIM2_CH3 由 CubeMX 配置） ---- */
    if (htim->Instance == TIM5)
    {
        GPIO_InitTypeDef gpio_cfg = {0};
        gpio_cfg.Pin       = GPIO_PIN_8;
        gpio_cfg.Mode      = GPIO_MODE_AF_PP;
        gpio_cfg.Pull      = GPIO_NOPULL;
        gpio_cfg.Speed     = GPIO_SPEED_FREQ_LOW;
        gpio_cfg.Alternate = 0x02;   /* STM32G474: PE8 AF2 = TIM5_CH3 */
        HAL_GPIO_Init(GPIOE, &gpio_cfg);
    }

    /* ------------------------------------------------------------
     *  HAL_TIM_PWM_Start 对 TIM2 返回 HAL_OK 但不设置 CCER（疑似
     *  HAL 库兼容性问题）。改用 CMSIS 级 TIM_CCxChannelCmd +
     *  __HAL_TIM_ENABLE，同时正确维护 htim->State 保持 HAL 契约。
     *  此方案同时兼容 TIM5 的多通道 HAL State 锁问题。
     * ------------------------------------------------------------ */
    {
        HAL_TIM_StateTypeDef prev_state = htim->State;
        htim->State = HAL_TIM_STATE_BUSY;
        TIM_CCxChannelCmd(htim->Instance, TIM_CHANNEL_3, TIM_CCx_ENABLE);
        if ((htim->Instance->CR1 & TIM_CR1_CEN) == 0) {
            __HAL_TIM_ENABLE(htim);
        }
        htim->State = prev_state;
    }

    /* 初始舵机置中位 */
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_3, mid_cmp);
    servo_handles[idx].initialized = true;

    /* ---- 调试输出（仅 SERVO_DEBUG 开启时编译） ---- */
#ifdef SERVO_DEBUG
    {
        /* 定时器状态验证 */
        SERVO_LOG("TIM%lu CH%lu PSC=%lu ARR=%lu CCR=%lu\r\n",
                  ((htim->Instance == TIM2) ? 2UL : 5UL),
                  3UL,
                  (uint32_t)htim->Init.Prescaler,
                  (uint32_t)htim->Init.Period,
                  (uint32_t)__HAL_TIM_GET_COMPARE(htim, TIM_CHANNEL_3));
        SERVO_LOG("TIM CR1=0x%04lX CNT=%lu CCER=0x%04lX (CH3=%lu)\r\n",
                  (uint32_t)htim->Instance->CR1,
                  (uint32_t)__HAL_TIM_GET_COUNTER(htim),
                  (uint32_t)htim->Instance->CCER,
                  ((htim->Instance->CCER >> 8) & 1UL));
    }
#endif
}

/* ================================================================
 *  Servo_SetAngle
 * ================================================================ */

void Servo_SetAngle(uint8_t ch, float angle)
{
    if (ch >= SERVO_TIM_CHANNEL_MAX) return;
    if (!servo_handles[ch].initialized) return;

    if (angle < 0.0f) angle = 0.0f;
    if (angle > SERVO_ANGLE_RANGE) angle = SERVO_ANGLE_RANGE;

    float    scale = (float)(servo_handles[ch].max_pulse
                           - servo_handles[ch].min_pulse)
                   / SERVO_ANGLE_RANGE;
    uint16_t pulse = (uint16_t)(servo_handles[ch].min_pulse
                                + angle * scale);

    __HAL_TIM_SET_COMPARE(servo_handles[ch].htim,
                          servo_handles[ch].channel, pulse);
}

/**
 * @brief  快速设置舵机为中位（90°）
 * @param  ch: 通道号
 */
void Servo_SetMid(uint8_t ch)
{
    if (ch >= SERVO_TIM_CHANNEL_MAX) return;
    if (!servo_handles[ch].initialized) return;

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
    if (servo_handles[ch].htim == NULL) return;

    HAL_TIM_PWM_Stop(servo_handles[ch].htim, servo_handles[ch].channel);
    servo_handles[ch].initialized = false;
}

/**
 * @brief  返回当前通道的近似角度（根据当前 CCR 计算）
 * @param  ch: 通道号
 * @return 角度值（0~180°）
 */
float Servo_GetAngle(uint8_t ch)
{
    if (ch >= SERVO_TIM_CHANNEL_MAX)          return -1.0f;
    if (!servo_handles[ch].initialized)       return -1.0f;

    uint16_t pulse = __HAL_TIM_GET_COMPARE(servo_handles[ch].htim,
                                            servo_handles[ch].channel);
    float scale = (float)(servo_handles[ch].max_pulse
                        - servo_handles[ch].min_pulse)
                / SERVO_ANGLE_RANGE;
    float angle = (pulse - servo_handles[ch].min_pulse) / scale;

    if (angle < 0.0f) angle = 0.0f;
    if (angle > SERVO_ANGLE_RANGE) angle = SERVO_ANGLE_RANGE;
    return angle;
}

/* ================================================================
 *  Servo_IsInitialized
 * ================================================================ */

bool Servo_IsInitialized(uint8_t ch)
{
    if (ch >= SERVO_TIM_CHANNEL_MAX) return false;
    return servo_handles[ch].initialized;
}
