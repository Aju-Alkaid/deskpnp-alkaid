#ifndef __TIMESTAMP_H
#define __TIMESTAMP_H

#include <stdint.h>

extern volatile uint32_t overflow_count ;

/**
 * @brief 初始化 TIM2 时间戳驱动
 * @note  必须在 CubeMX 中开启 TIM2 的溢出中断 (Update Interrupt)
 */
void TIM2_Timestamp_Init(void);

/**
 * @brief 获取 32 位当前时间戳 (微秒级)
 * @return TIM2 CNT 寄存器的当前值
 * @note  适用于短时间间隔测量 (< 65ms)
 */
uint32_t TIM2_Get_Current_Timestamp_32b(void);

/**
 * @brief 获取 64 位当前时间戳 (微秒级)
 * @return 累加了溢出次数的 64 位时间戳
 * @note  适用于长时间运行，防止溢出
 */
uint64_t TIM2_Get_Current_Timestamp_64b(void);

/**
 * @brief 计算时间差 (Delta Time)
 * @param last_time 上一次记录的时间戳
 * @return 两次时间戳之间的时间差 (微秒)
 * @note  自动处理溢出回绕
 */
uint32_t TIM2_Calculate_Delta_Time(uint32_t last_time);

/**
 * @brief 微秒级延时
 * @param us 需要延时的微秒数
 * @note  利用 TIM2 自由运行计数实现，精度极高
 */
void TIM2_Delay_us(uint32_t us);


/**
 * @brief 毫秒级延时
 * @param ms 要延时的毫秒数
 */
void TIM2_Delay_ms(uint32_t ms);


#endif // __SYS_TIMESTAMP_H