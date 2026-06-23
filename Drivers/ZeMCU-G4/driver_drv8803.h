#ifndef __DRV8803_DUAL_H
#define __DRV8803_DUAL_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

// ==================== 引脚结构体定义 ====================
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} Pin_t;

typedef struct {
    uint8_t num_pins;   // 实际使用的引脚数（1~2）
    Pin_t   pins[2];    // pins[0]=开关引脚, pins[1]=PWM引脚(仅O3/O4)
} PowerPort_t;

// ==================== 8 个物理输出端口实例（全局常量） ====================
extern const PowerPort_t Port_12VO1;   // PE11（仅开关）
extern const PowerPort_t Port_12VO2;   // PE12（仅开关）
extern const PowerPort_t Port_12VO3;   // PE13 + PE8(PWM)
extern const PowerPort_t Port_12VO4;   // PE14 + PB10(PWM)
extern const PowerPort_t Port_24VO1;   // PA6（仅开关）
extern const PowerPort_t Port_24VO2;   // PA7（仅开关）
extern const PowerPort_t Port_24VO3;   // PC4 + PB1(PWM)
extern const PowerPort_t Port_24VO4;   // PC5 + PB2(PWM)

// ==================== PWM 引脚便捷宏 ====================
#define PWM_12VO3_PIN  Port_12VO3.pins[1]   // PE8  TIM5_CH3
#define PWM_12VO4_PIN  Port_12VO4.pins[1]   // PB10 TIM2_CH3
#define PWM_24VO3_PIN  Port_24VO3.pins[1]   // PB1  TIM3_CH4
#define PWM_24VO4_PIN  Port_24VO4.pins[1]   // PB2  TIM5_CH1

// ==================== 芯片级控制引脚（EN/RESET/FAULT，非输出通道） ====================
#define DRV1_EN_PORT        GPIOE
#define DRV1_EN_PIN         GPIO_PIN_9
#define DRV1_RESET_PORT     GPIOE
#define DRV1_RESET_PIN      GPIO_PIN_10
#define DRV1_FAULT_PORT     GPIOE
#define DRV1_FAULT_PIN      GPIO_PIN_15

#define DRV2_EN_PORT        GPIOA
#define DRV2_EN_PIN         GPIO_PIN_4
#define DRV2_RESET_PORT     GPIOB
#define DRV2_RESET_PIN      GPIO_PIN_0
#define DRV2_FAULT_PORT     GPIOA
#define DRV2_FAULT_PIN      GPIO_PIN_5

// ==================== API 函数 ====================

// 初始化所有输出端口为低电平，禁用两个芯片
HAL_StatusTypeDef DRV8803_Init(void);

// 控制单个输出端口开关（仅操作 pins[0] 开关引脚）
void DRV8803_SetOutput(const PowerPort_t *port, bool on);

// 芯片级使能/禁用（chip: 1=12V(U12), 2=24V(U13)）
void DRV8803_EnableChip(uint8_t chip_id, bool enable);

// 读取芯片故障状态
bool DRV8803_IsChipFault(uint8_t chip_id);

// 触发芯片硬件复位
void DRV8803_TriggerChipReset(uint8_t chip_id);

// 故障恢复处理（FreeRTOS 任务中调用）
void DRV8803_HandleFault_RTOS(uint8_t chip_id);


// ==================== 语义化端口宏 ====================
#define PUMP_PORT   (&Port_12VO1)   // 12VO1/PE11 接气泵
#define VALVE_PORT  (&Port_24VO1)   // 24VO1/PA6 接电磁阀

// ==================== 语义化接口函数（静态内联） ====================

/** @brief 气泵开启（12VO1/PE11） */
static inline void Pump_On(void) {
    DRV8803_SetOutput(&Port_12VO1, true);
}

/** @brief 气泵关闭（12VO1/PE11） */
static inline void Pump_Off(void) {
    DRV8803_SetOutput(&Port_12VO1, false);
}

/** @brief 电磁阀开启（24VO1/PA6, 实际电路: PA6=HIGH→导通） */
static inline void Valve_On(void) {
    DRV8803_SetOutput(&Port_24VO1, true);
}

/** @brief 电磁阀关闭（24VO1/PA6, 实际电路: PA6=LOW→断开） */
static inline void Valve_Off(void) {
    DRV8803_SetOutput(&Port_24VO1, false);
}

/** @brief 下位相机补光灯开启（12VO2/PE12） */
static inline void LowerCam_Light_On(void) {
    DRV8803_SetOutput(&Port_12VO2, true);
}

/** @brief 下位相机补光灯关闭（12VO2/PE12） */
static inline void LowerCam_Light_Off(void) {
    DRV8803_SetOutput(&Port_12VO2, false);
}
#endif /* __DRV8803_DUAL_H */
