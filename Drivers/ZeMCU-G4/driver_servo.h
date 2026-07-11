#ifndef __DRIVER_SERVO_H
#define __DRIVER_SERVO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"

/* ================================================================
 *  编译开关
 * ================================================================ */

/*
 * SERVO_DEBUG — 启用后 Servo_Init() 会通过 PrintDebug() 输出
 * GPIO AF / TIM5 寄存器诊断信息。调试完成后注释掉即可。
 *
 * 注意：启用时需确保 PrintDebug() 在链接范围内（app_test.c）。
 */
/* #define SERVO_DEBUG */

/* ================================================================
 *  舵机物理参数
 * ================================================================ */

#define SERVO_PWM_PERIOD_MS   20.0f          /* PWM 周期 20ms (50Hz) */
#define SERVO_PWM_MIN_US      500            /* 最小脉宽对应 0° (us) */
#define SERVO_PWM_MAX_US      2500           /* 最大脉宽对应 180° (us) */
#define SERVO_ANGLE_RANGE     180.0f         /* 角度范围 0~180° */

/* ================================================================
 *  硬件映射 — TIM5_CH3 → PE8 → Z轴舵机 (MG995)
 * ================================================================ */

#define SERVO_TIM_CHANNEL_MAX  4              /* 最多支持 4 路舵机 */

/*
 * CubeMX 参考值（仅作文档用，实际值从 htim->Init 读出）：
 *   htim5.Init.Prescaler = 169   → 170MHz / 170 = 1MHz tick
 *   htim5.Init.Period    = 19999 → 1MHz / 20000 = 50Hz
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║ 已知 CubeMX Bug：PE8 的 TIM5_CH3 在 STM32G474 上是 AF2，  ║
 * ║ 但 CubeMX 生成的 HAL_TIM_MspPostInit 里写成了 AF1。       ║
 * ║ Servo_Init() 内会重新把 PE8 初始化为正确的 AF2。          ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

/* ================================================================
 *  数据结构
 * ================================================================ */

typedef struct {
    TIM_HandleTypeDef *htim;                 /* 定时器句柄 */
    uint32_t           channel;              /* 定时器通道 (TIM_CHANNEL_x) */
    uint32_t           min_pulse;            /* 0° 对应的 CCR 比较值 */
    uint32_t           max_pulse;            /* 180° 对应的 CCR 比较值 */
    uint32_t           mid_pulse;            /* 90° 对应的 CCR 比较值 */
    bool               initialized;          /* 是否已完成初始化 */
} Servo_HandleTypeDef;

/* ================================================================
 *  API
 * ================================================================ */

/** 初始化舵机模块。应在系统启动阶段调用一次。 */
void Servo_Init(TIM_HandleTypeDef *htim);

/** 设置指定通道的舵机角度（0~180°），非阻塞。 */
void Servo_SetAngle(uint8_t ch, float angle);

/** 快速转到中位 90°。 */
void Servo_SetMid(uint8_t ch);

/** 关闭指定通道的 PWM 输出（释放舵机力矩）。 */
void Servo_Stop(uint8_t ch);

/** 返回当前近似角度（°）。未初始化返回 -1。 */
float Servo_GetAngle(uint8_t ch);

/** 返回通道是否已完成初始化。 */
bool Servo_IsInitialized(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* __DRIVER_SERVO_H */
