/**
 * @file    driver_kth7823.c
 * @brief   KTH7823 14-bit 磁编码器驱动 — TIM5 CH1 双边沿输入捕获 + 角度解算
 *
 * 硬件连接: KTH7823 PWM 输出 → PB2 (TIM5_CH1)
 * 定时器:   TIM5, PSC=0, 170MHz, 32-bit
 * 编码器 PWM: 910Hz, 脉宽 32~16416/16448 单位对应 0~360°
 *
 * 角度公式: Ang = (360/16384) * [ (16448 * tON) / period - 32 ]
 */

#include "driver_kth7823.h"
#include "tim.h"
#include <math.h>
#include "FreeRTOS.h"
#include "task.h"

/* ---- 硬件引脚 ---- */
#define KTH7823_PWM_PORT    GPIOB
#define KTH7823_PWM_PIN     GPIO_PIN_2

/* ---- 输入滤波: N=8 → ~47ns @170MHz fDTS, 远小于 2μs 最小脉宽 ---- */
#define KTH7823_IC_FILTER   8u

/* ---- 内部状态 ----
 * 所有 volatile 成员由 ISR 写入、任务上下文读取。
 * struct 为 static → 编译期零初始化, 无需运行时 memset。 */

static struct {
    volatile uint32_t rising_ccr;       /* 当前周期上升沿 CCR */
    volatile uint32_t prev_rising_ccr;  /* 上一周期上升沿 (计算周期用) */
    volatile uint32_t falling_ccr;      /* 下降沿 CCR */
    volatile uint32_t tON_ticks;        /* 高电平脉宽 (ticks) */
    volatile uint32_t period_ticks;     /* 当前 PWM 周期 (ticks) */
    volatile float    angle_deg;        /* 最新解算角度 (°) */
    volatile bool     data_ready;       /* 新数据待读取 */
    bool              have_prev_rising; /* 已捕获至少一次上升沿 */
    bool              initialized;      /* HAL_TIM_IC_Start_IT 成功 */
} g_kth;

/* ---- 内部辅助 ---- */

/**
 * @brief KTH7823 角度解算 (14-bit, 910Hz PWM)
 * @param tON    高电平脉宽 (timer ticks)
 * @param period PWM 周期 (timer ticks)
 * @return 角度 0~360°
 *
 * 公式: Ang = (360/16384) * [ (16448 * tON) / period - 32 ]
 * ratio 为 0~16384 的浮点数, 截断保护防止噪声造成的越界。
 */
static float kth_compute_angle(uint32_t tON, uint32_t period) {
    if (period == 0) return 0.0f;
    float ratio = (KTH7823_UNIT_TOTAL * (float)tON) / (float)period
                - KTH7823_UNIT_OFFSET;
    if (ratio < 0.0f)                     ratio = 0.0f;
    if (ratio > KTH7823_COUNTS_PER_REV)   ratio = KTH7823_COUNTS_PER_REV;
    return (360.0f / KTH7823_COUNTS_PER_REV) * ratio;
}

/* ---- 初始化 ---- */

bool KTH7823_Init(void) {
    /* struct 已是编译期零初始化; 若需重复 Init, 显式复位关键字段 */
    g_kth.have_prev_rising = false;
    g_kth.initialized      = false;
    g_kth.data_ready       = false;

    /* 配置输入滤波器: N=8 → 毛刺 < ~47ns 被抑制, 远小于最短脉宽 ~2μs
     * 直接写 CCMR1 寄存器 — CubeMX 生成的 ICFilter=0 在此被覆写。
     * 若重新生成 CubeMX 代码, 确保 KTH7823_Init 仍在 TIM5 初始化之后调用。 */
    uint32_t ccmr1 = TIM5->CCMR1;
    ccmr1 &= ~TIM_CCMR1_IC1F;
    ccmr1 |= (KTH7823_IC_FILTER << TIM_CCMR1_IC1F_Pos);
    TIM5->CCMR1 = ccmr1;

    /* 启动 CH1 双边沿输入捕获中断 */
    if (HAL_TIM_IC_Start_IT(KTH7823_TIM, KTH7823_TIM_CHANNEL) != HAL_OK) {
        return false;
    }
    g_kth.initialized = true;
    return true;
}

/* ---- 角度读取 (任务上下文调用) ---- */

/**
 * @brief 获取最新角度。
 * 先快照 angle_deg 再清 data_ready, 避免 ISR 在清 flag 与读值之间
 * 更新数据导致本轮返回旧值且丢失新数据的标志位。
 */
float KTH7823_GetAngle(void) {
    if (!g_kth.initialized) return 0.0f;
    float val = g_kth.angle_deg;   /* 先读值 (Cortex-M4 单周期 VLDR, 32-bit 原子) */
    g_kth.data_ready = false;      /* 后清标志 */
    return val;
}

bool KTH7823_IsDataReady(void) {
    return g_kth.initialized && g_kth.data_ready;
}

/**
 * @brief 阻塞等待首个有效编码器读数。
 * 用于 r_axis_set_zero 等场景, 确保零点偏置基于真实编码器值。
 */
bool KTH7823_WaitData(uint32_t timeout_ms) {
    if (!g_kth.initialized) return false;
    uint32_t t0 = HAL_GetTick();
    while (!g_kth.data_ready) {
        if (HAL_GetTick() - t0 >= timeout_ms) return false;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return true;
}

/* ---- HAL 输入捕获回调 (ISR 上下文) ---- */

/**
 * @brief 重写 __weak HAL_TIM_IC_CaptureCallback
 *
 * 双边沿捕获: CC1 中断在上升沿/下降沿均触发。
 * 通过读 PB2 引脚电平区分边沿:
 *   - HIGH → 上升沿: 记录 CCR + 计算周期
 *   - LOW  → 下降沿: 计算 tON + 解算角度
 *
 * 首周期不产出角度 (period 未知), 第二周期起每周期更新一次 (910Hz)。
 *
 * 32-bit 计数器 @170MHz 回绕周期 ~25.3s, 两次捕获间隔 <200k ticks,
 * 无符号减法天然处理同向回绕, 无需额外逻辑。
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance != TIM5 || htim->Channel != HAL_TIM_ACTIVE_CHANNEL_1) {
        return;
    }

    uint32_t ccr = TIM5->CCR1;  /* 直读寄存器, 比 HAL 更快 (ISR 性能关键路径) */

    if (HAL_GPIO_ReadPin(KTH7823_PWM_PORT, KTH7823_PWM_PIN) == GPIO_PIN_SET) {
        /* ---- 上升沿 ---- */
        g_kth.rising_ccr = ccr;

        if (g_kth.have_prev_rising) {
            g_kth.period_ticks = g_kth.rising_ccr - g_kth.prev_rising_ccr;
        }
        g_kth.prev_rising_ccr = ccr;
        g_kth.have_prev_rising = true;

    } else {
        /* ---- 下降沿 ---- */
        if (!g_kth.have_prev_rising) return;

        g_kth.falling_ccr = ccr;
        g_kth.tON_ticks   = g_kth.falling_ccr - g_kth.rising_ccr;

        /* 有效性检查: 周期已建立 且 脉宽不超过周期 */
        if (g_kth.period_ticks > 0
            && g_kth.tON_ticks <= g_kth.period_ticks) {
            g_kth.angle_deg = kth_compute_angle(g_kth.tON_ticks,
                                                g_kth.period_ticks);
            g_kth.data_ready = true;
        }
    }
}