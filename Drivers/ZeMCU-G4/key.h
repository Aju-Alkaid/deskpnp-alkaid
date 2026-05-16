#ifndef __KEY_H__
#define __KEY_H__


#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ======================== 用户配置区域 ========================
#define KEY_NUM         5   // 按键数量

// 按键引脚定义（根据实际接线）
// KEY1
#define KEY1_GPIO_PORT  GPIOC
#define KEY1_GPIO_PIN   GPIO_PIN_6
#define KEY1_ACTIVE_LEVEL  GPIO_PIN_RESET   // 按下为低电平

// KEY2
#define KEY2_GPIO_PORT  GPIOC
#define KEY2_GPIO_PIN   GPIO_PIN_7
#define KEY2_ACTIVE_LEVEL  GPIO_PIN_RESET

// CW (顺时针旋转)
#define KEY_CW_GPIO_PORT  GPIOC
#define KEY_CW_GPIO_PIN   GPIO_PIN_8
#define KEY_CW_ACTIVE_LEVEL  GPIO_PIN_RESET

// CCW (逆时针旋转)
#define KEY_CCW_GPIO_PORT GPIOA
#define KEY_CCW_GPIO_PIN  GPIO_PIN_8
#define KEY_CCW_ACTIVE_LEVEL GPIO_PIN_RESET

// PUSH (按压)
#define KEY_PUSH_GPIO_PORT GPIOC
#define KEY_PUSH_GPIO_PIN  GPIO_PIN_9
#define KEY_PUSH_ACTIVE_LEVEL GPIO_PIN_RESET

// // 开发板Key
// #define KEY_dev_GPIO_PORT GPIOD
// #define KEY_dev_GPIO_PIN  GPIO_PIN_15
// #define KEY_dev_ACTIVE_LEVEL GPIO_PIN_RESET

// 消抖时间（毫秒），建议10~30ms
#define KEY_DEBOUNCE_MS    20

// // 按键返回值定义（与原来保持一致）
#define key1    0   // KEY1
#define key2    1   // KEY2
#define down    2   // CW顺时针//down
#define up      3   // CCW逆时针//wp
#define push    4   // PUSH


// ======================== API 函数 ========================
void Key_Init(void);
void Key_Scan(void);                // 需在主循环中周期调用（建议间隔10ms）
uint8_t Key_GetEvent(uint8_t key_id); // 返回单击事件(1) 或 0
uint8_t Key_IsAnyPressed(void);        // 返回当前被按下按键的键值（实时电平）
void Key_ClearEvent(uint8_t key_id);   // 清除事件

#ifdef __cplusplus
}
#endif

#endif
