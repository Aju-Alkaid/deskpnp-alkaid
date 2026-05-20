#ifndef __KEY_H__
#define __KEY_H__

#include "main.h"
#include "cmsis_os2.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ======================== 用户配置区域 ========================
#define KEY_NUM         5   // 按键数量

// 按键引脚定义
// KEY1
#define KEY1_GPIO_PORT  GPIOC
#define KEY1_GPIO_PIN   GPIO_PIN_6
#define KEY1_ACTIVE_LEVEL  GPIO_PIN_RESET

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

// 消抖时间（毫秒）
#define KEY_DEBOUNCE_MS    10

// 按键返回值定义（加 KEY_ 前缀避免命名冲突）
#define KEY_KEY1    0   // KEY1
#define KEY_KEY2    1   // KEY2
#define KEY_DOWN    2   // CW顺时针 / down
#define KEY_UP      3   // CCW逆时针 / up
#define KEY_PUSH    4   // PUSH

// ======================== API 函数 ========================
void Key_Init(void);
void Key_Scan(void);

// 获取按键松开事件（原有接口，保持兼容）
uint8_t Key_GetEvent(uint8_t key_id);

// 获取按键按下事件（新增：按下一瞬间即触发）
uint8_t Key_GetPressEvent(uint8_t key_id);

// 查询当前是否有按键被按下（实时电平）
uint8_t Key_IsAnyPressed(void);

// 清除按键事件
void Key_ClearEvent(uint8_t key_id);
void Key_ClearPressEvent(uint8_t key_id);

// ---- FreeRTOS 集成 ----
// 按键事件结构体
typedef struct {
    uint8_t key_id;   // 按键 ID
    uint8_t type;     // 0=释放(click), 1=按下(press)
} KeyEvent_t;

// 按键事件队列句柄（由 app_freertos.c 初始化）
extern osMessageQueueId_t keyEventQueue;

// 按键任务入口（在 app_freertos.c 中创建）
void Key_Task(void *argument);

#ifdef __cplusplus
}
#endif

#endif