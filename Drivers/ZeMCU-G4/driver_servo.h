#ifndef __DRIVER_SERVO_H
#define __DRIVER_SERVO_H


#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"

/* 舵机物理参数 ------------------------------ */
#define SERVO_PWM_PERIOD_MS   20.0f          /* PWM 周期 20ms */
#define SERVO_PWM_MIN_US      500            /* 最小脉宽对应 0° (us) */
#define SERVO_PWM_MAX_US      2500           /* 最大脉宽对应 180° (us) */
#define SERVO_ANGLE_RANGE     180.0f         /* 角度范围 0~180° */

/* 定时器配置（需在 CubeMX 中设置 TIMx 产生 50Hz PWM） */
#define SERVO_TIM             htim5          /* 使用 TIM5 */
#define SERVO_TIM_CHANNEL_MAX 4              /* 最多支持 4 路舵机，可根据实际修改 */
#define SERVO_TIM5_PSC_VALUE    (uint32_t)169    // 对应 htim5.Init.Prescaler
#define SERVO_TIM5_ARR_VALUE    (uint32_t)19999  // 对应 htim5.Init.Period
/* 全局舵机句柄结构体 */
typedef struct {
    TIM_HandleTypeDef *htim;                 /* 定时器句柄 */
    uint32_t          channel;               /* 定时器通道 */
    uint16_t          min_pulse;             /* 最小脉冲比较值 */
    uint16_t          max_pulse;             /* 最大脉冲比较值 */
    uint16_t          mid_pulse;             /* 中位脉冲比较值 (调试用) */
} Servo_HandleTypeDef;

/* 函数声明 -------------------------------------------------- */
/* 初始化舵机驱动模块，配置定时器（应在 main.c 中调用） */
void Servo_Init(TIM_HandleTypeDef *htim);

/* 设置指定通道的舵机角度（0~180°），非阻塞 */
void Servo_SetAngle(uint8_t ch, float angle);

/* 将舵机转到中位 90° */
void Servo_SetMid(uint8_t ch);

/* 关闭指定通道的 PWM 输出（释放舵机力矩） */
void Servo_Stop(uint8_t ch);

/* 获取当前通道的角度（近似值） */
float Servo_GetAngle(uint8_t ch);

#ifdef __cplusplus
}
#endif












#endif
