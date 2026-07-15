#ifndef __DRIVER_KTH7823_H
#define __DRIVER_KTH7823_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  KTH7823 磁编码器 — PWM 绝对位置输出参数 (910Hz, 14-bit)
 * ================================================================ */

#define KTH7823_PWM_FREQ_HZ      910.0f      /* PWM 载波频率 */
#define KTH7823_UNIT_TOTAL       16448.0f    /* 总单位时间 = 16384 + 64 */
#define KTH7823_UNIT_OFFSET      32.0f       /* 0° 对应的最小单位数 */
#define KTH7823_COUNTS_PER_REV   16384.0f    /* 一圈的分辨率 (14-bit) */

/* 定时器参数 (TIM5 CH1, PSC=0 → 170MHz) */
#define KTH7823_TIM              (&htim5)
#define KTH7823_TIM_CHANNEL      TIM_CHANNEL_1
#define KTH7823_TIM_CLK_HZ       170000000UL
#define KTH7823_TICK_PER_US      170.0f      /* 1μs = 170 ticks */

/* ---- API ---- */

/** 初始化编码器驱动，启动 TIM5 CH1 双边沿输入捕获中断。
 *  同时设置输入滤波器 (ICFilter=8, ~47ns)，抑制电磁干扰毛刺。
 *  @return true=成功, false=HAL_TIM_IC_Start_IT 失败 */
bool KTH7823_Init(void);

/** 获取最新角度 (°)，调用后自动清除 data_ready 标志。未初始化返回 0。 */
float KTH7823_GetAngle(void);

/** 是否有新的角度数据就绪（由 ISR 置位）。 */
bool KTH7823_IsDataReady(void);

/** 阻塞等待首个有效编码器数据。
 * @param timeout_ms 超时时间 (ms)，0 表示仅轮询一次
 * @return true=成功获取数据, false=超时 */
bool KTH7823_WaitData(uint32_t timeout_ms);

/* 诊断接口 */
typedef struct {
    uint32_t rising_count;     /* ISR 上升沿计数 */
    uint32_t falling_count;    /* ISR 下降沿计数 */
    uint32_t valid_count;      /* 有效角度计算次数 */
    uint32_t last_tON_ticks;   /* 最近一次 tON (ticks) */
    uint32_t last_period_ticks;/* 最近一次 period (ticks) */
} KTH7823_Debug_t;
void KTH7823_GetDebug(KTH7823_Debug_t *dbg);

#ifdef __cplusplus
}
#endif

#endif /* __DRIVER_KTH7823_H */