#include "timestamp.h"
#include "stm32g4xx_hal.h"
#include "main.h" 

extern TIM_HandleTypeDef htim2;

// 用于记录 TIM2 的溢出次数
// volatile 关键字确保编译器每次读取内存而不是寄存器缓存
volatile uint32_t overflow_count = 0;

/**
 * @brief TIM2 中断服务函数
 * @note  必须在 stm32g4xx_it.c 中调用此函数，或直接在此编写 ISR(已将其搬到main.c中使用，后续需要更改直接在main.c中更改）
 */
 
//void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
//    if (htim->Instance == TIM2) {
//        // 每次溢出，计数器加 1
//        overflow_count++;
//    }
//}

/**
 * @brief 初始化 TIM2 作为时间戳源
 */
void TIM2_Timestamp_Init(void) {
    // 1. 重置溢出计数器
    overflow_count = 0;

    // 2. 启动 TIM2 计数器
    // 注意：必须在 CubeMX 中开启 Update 事件中断 (Update Interrupt)
    if (HAL_TIM_Base_Start(&htim2) != HAL_OK) {
        // 启动错误处理
       Error_Handler();
    }
}

/**
 * @brief 获取 32 位当前时间戳
 */
uint32_t TIM2_Get_Current_Timestamp_32b(void) {
    return __HAL_TIM_GET_COUNTER(&htim2);
}

/**
 * @brief 获取 64 位当前时间戳
 */
uint64_t TIM2_Get_Current_Timestamp_64b(void) {
    uint32_t cnt, ovf1, ovf2;

    // 双读法防止在读取过程中发生溢出中断
    ovf1 = overflow_count;
    cnt = __HAL_TIM_GET_COUNTER(&htim2);
    ovf2 = overflow_count;

    // 如果在读取 cnt 期间发生了溢出，ovf1 和 ovf2 会不同
    if (ovf1 != ovf2) {
        // 重新读取
        cnt = __HAL_TIM_GET_COUNTER(&htim2);
        ovf1 = overflow_count;
    }

    // 假设 TIM2 的 ARR 是 0xFFFF (65535)，即每 65.5ms 溢出一次
    // 这里假设 TIM2 是 16 位计数器
    return ((uint64_t)ovf1 << 16) | (uint64_t)cnt;
}

/**
 * @brief 计算时间差 (Delta Time)
 */
uint32_t TIM2_Calculate_Delta_Time(uint32_t last_time) {
    uint32_t now = __HAL_TIM_GET_COUNTER(&htim2);

    // 自动处理溢出回绕
    // 如果 now < last_time，说明发生了溢出，需要加上溢出周期的长度
    if (now >= last_time) {
        return now - last_time;
    } else {
        // 假设 TIM2 自动重装载值是 0xFFFF (65535)
        // 这里的 0xFFFF 可以替换为宏定义，如 #define TIM2_PERIOD 65535
        return (0xFFFF - last_time) + now;
    }
}

/**
 * @brief 微秒级延时
 */
void TIM2_Delay_us(uint32_t us) {
    uint32_t start = __HAL_TIM_GET_COUNTER(&htim2);
    uint32_t delta = 0;

    while (delta < us) {
        uint32_t current = __HAL_TIM_GET_COUNTER(&htim2);
        if (current >= start) {
            delta = current - start;
        } else {
            // 处理溢出
            delta = (0xFFFF - start) + current;
        }
    }
}

/**
 * @brief 毫秒级延时
 * @param ms 要延时的毫秒数
 */
void TIM2_Delay_ms(uint32_t ms) {
    TIM2_Delay_us(ms * 1000);
}