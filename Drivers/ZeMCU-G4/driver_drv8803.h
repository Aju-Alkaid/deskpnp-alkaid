#ifndef __DRV8803_DUAL_H
#define __DRV8803_DUAL_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

// 便捷宏
#define DRV1_SetCh1(state) DRV8803_SetGlobalChannel(CH1, state)
#define DRV1_SetCh2(state) DRV8803_SetGlobalChannel(CH2, state)
#define DRV1_SetCh3(state) DRV8803_SetGlobalChannel(CH3, state)
#define DRV1_SetCh4(state) DRV8803_SetGlobalChannel(CH4, state)

#define DRV2_SetCh1(state) DRV8803_SetGlobalChannel(CH5, state)
#define DRV2_SetCh2(state) DRV8803_SetGlobalChannel(CH6, state)
#define DRV2_SetCh3(state) DRV8803_SetGlobalChannel(CH7, state)
#define DRV2_SetCh4(state) DRV8803_SetGlobalChannel(CH8, state)

// ==================== U12: 12V 负载芯片 ====================
#define DRV1_EN_PORT        GPIOE
#define DRV1_EN_PIN         GPIO_PIN_9      // nENBL1

#define DRV1_RESET_PORT     GPIOE
#define DRV1_RESET_PIN      GPIO_PIN_10     // RESET1

#define DRV1_IN1_PORT       GPIOE
#define DRV1_IN1_PIN        GPIO_PIN_14     // IN1

#define DRV1_IN2_PORT       GPIOE
#define DRV1_IN2_PIN        GPIO_PIN_13     // IN2

#define DRV1_IN3_PORT       GPIOE
#define DRV1_IN3_PIN        GPIO_PIN_12     // IN3

#define DRV1_IN4_PORT       GPIOE
#define DRV1_IN4_PIN        GPIO_PIN_11     // IN4

#define DRV1_FAULT_PORT     GPIOE
#define DRV1_FAULT_PIN      GPIO_PIN_15     // nFAULT1

// 12V PWM 控制线（非 DRV8803 引脚）
#define PWM_12V_C1_PORT     GPIOB
#define PWM_12V_C1_PIN      GPIO_PIN_10     // 12V_C1

//#define PWM_12V_C2_PORT     GPIOE
//#define PWM_12V_C2_PIN      GPIO_PIN_8      // 12V_C2

// ==================== U13: 24V 负载芯片 ====================
#define DRV2_EN_PORT        GPIOA
#define DRV2_EN_PIN         GPIO_PIN_4      // nENBL2

#define DRV2_RESET_PORT     GPIOB
#define DRV2_RESET_PIN      GPIO_PIN_0      // RESET2

#define DRV2_IN1_PORT       GPIOA
#define DRV2_IN1_PIN        GPIO_PIN_6      // IN5 (对应 OUT5)

#define DRV2_IN2_PORT       GPIOA
#define DRV2_IN2_PIN        GPIO_PIN_7      // IN6 (对应 OUT6)

#define DRV2_IN3_PORT       GPIOC
#define DRV2_IN3_PIN        GPIO_PIN_4      // IN7 (对应 OUT7)

#define DRV2_IN4_PORT       GPIOC
#define DRV2_IN4_PIN        GPIO_PIN_5      // IN8 (对应 OUT8)

#define DRV2_FAULT_PORT     GPIOA
#define DRV2_FAULT_PIN      GPIO_PIN_5      // nFAULT2

// 24V PWM 控制线
#define PWM_24V_C1_PORT     GPIOB
#define PWM_24V_C1_PIN      GPIO_PIN_2      // 24V_C1

#define PWM_24V_C2_PORT     GPIOB
#define PWM_24V_C2_PIN      GPIO_PIN_1      // 24V_C2

// 通道枚举 (全局统一编号 0-7)
typedef enum {
    CH1 = 0,  // U12 OUT1
    CH2 = 1,  // U12 OUT2
    CH3 = 2,  // U12 OUT3
    CH4 = 3,  // U12 OUT4
    CH5 = 4,  // U13 OUT5
    CH6 = 5,  // U13 OUT6
    CH7 = 6,  // U13 OUT7
    CH8 = 7   // U13 OUT8
} GlobalChannel_t;

// 初始化函数（仅配置初始电平，不修改 GPIO 模式）
HAL_StatusTypeDef DRV8803_Dual_Config(void);

// 全局使能/禁用单个芯片
void DRV8803_EnableChip(uint8_t chip_id, bool enable);

// 设置单个通道状态 (全局通道号 0-7)
void DRV8803_SetGlobalChannel(GlobalChannel_t ch, bool state);

// 批量设置某芯片的所有通道
void DRV8803_SetChipChannels(uint8_t chip_id, uint8_t channel_mask);

// 读取单个芯片故障状态
bool DRV8803_IsChipFault(uint8_t chip_id);

// 触发单个芯片硬件复位
void DRV8803_TriggerChipReset(uint8_t chip_id);

// 故障恢复（适用于 FreeRTOS 任务，非阻塞）
void DRV8803_HandleFault_RTOS(uint8_t chip_id);



#endif /* __DRV8803_DUAL_H */