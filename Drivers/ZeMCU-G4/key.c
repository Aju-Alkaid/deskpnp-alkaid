#include "key.h"
#include "stm32g4xx_hal.h"
#include <string.h>

// 按键事件队列句柄（在 app_freertos.c 的 MX_FREERTOS_Init 中创建）
osMessageQueueId_t keyEventQueue = NULL;

// 按键硬件配置表
typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
    GPIO_PinState active_level;
} KeyHardware_t;

static const KeyHardware_t key_hw[KEY_NUM] = {
    {KEY1_GPIO_PORT, KEY1_GPIO_PIN, KEY1_ACTIVE_LEVEL},
    {KEY2_GPIO_PORT, KEY2_GPIO_PIN, KEY2_ACTIVE_LEVEL},
    {KEY_CW_GPIO_PORT, KEY_CW_GPIO_PIN, KEY_CW_ACTIVE_LEVEL},
    {KEY_CCW_GPIO_PORT, KEY_CCW_GPIO_PIN, KEY_CCW_ACTIVE_LEVEL},
    {KEY_PUSH_GPIO_PORT, KEY_PUSH_GPIO_PIN, KEY_PUSH_ACTIVE_LEVEL},
};

// 按键状态结构体
typedef struct {
    uint8_t pressed;        // 当前是否处于"已按下并确认"状态
    uint8_t event;          // 0=无事件, 1=单击事件(松开时)
    uint8_t event_press;    // 0=无事件, 1=按下事件(按下一瞬间)
    uint8_t debounce_cnt;   // 消抖计数器
} KeyStatus_t;

static KeyStatus_t key_status[KEY_NUM];

// 获取当前按键电平（按下返回1，未按下返回0）
static uint8_t Key_GetLevel(uint8_t key_id)
{
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(key_hw[key_id].port, key_hw[key_id].pin);
    return (pin_state == key_hw[key_id].active_level) ? 1 : 0;
}

// 初始化GPIO（仅在首次调用时配置硬件）
void Key_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    for (uint8_t i = 0; i < KEY_NUM; i++) {
        gpio_init.Pin = key_hw[i].pin;
        gpio_init.Mode = GPIO_MODE_INPUT;
        gpio_init.Pull = (key_hw[i].active_level == GPIO_PIN_RESET) ? GPIO_PULLUP : GPIO_PULLDOWN;
        gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(key_hw[i].port, &gpio_init);
    }
    memset(key_status, 0, sizeof(key_status));
}

// 按键扫描函数（每10ms调用一次）
void Key_Scan(void)
{
    static const uint8_t debounce_samples = KEY_DEBOUNCE_MS / 10;
    if (debounce_samples == 0) return;

    for (uint8_t i = 0; i < KEY_NUM; i++) {
        uint8_t current_level = Key_GetLevel(i);
        KeyStatus_t *ks = &key_status[i];

        if (current_level) { // 当前读到按下
            if (!ks->pressed) {
                // 尚未确认按下，进行按下消抖
                ks->debounce_cnt++;
                if (ks->debounce_cnt >= debounce_samples) {
                    ks->pressed = 1;          // 确认按下
                    ks->event_press = 1;      // 按下一瞬间即上报事件
                    ks->debounce_cnt = 0;
                }
            } else {
                ks->debounce_cnt = 0;
            }
        } else { // 当前读到释放
            if (ks->pressed) {
                // 已确认按下，现在检测释放 → 进行释放消抖
                ks->debounce_cnt++;
                if (ks->debounce_cnt >= debounce_samples) {
                    // 确认释放，产生单击事件
                    if (ks->event == 0) {
                        ks->event = 1;
                    }
                    ks->pressed = 0;
                    ks->debounce_cnt = 0;
                }
            } else {
                ks->debounce_cnt = 0;
            }
        }
    }
}

// 获取按键松开事件（原有接口，保持兼容）
uint8_t Key_GetEvent(uint8_t key_id)
{
    if (key_id >= KEY_NUM) return 0;
    return key_status[key_id].event;
}

// 获取按键按下事件（新增：按下一瞬间即触发）
uint8_t Key_GetPressEvent(uint8_t key_id)
{
    if (key_id >= KEY_NUM) return 0;
    return key_status[key_id].event_press;
}

// 清除按键松开事件
void Key_ClearEvent(uint8_t key_id)
{
    if (key_id >= KEY_NUM) return;
    key_status[key_id].event = 0;
}

// 清除按键按下事件
void Key_ClearPressEvent(uint8_t key_id)
{
    if (key_id >= KEY_NUM) return;
    key_status[key_id].event_press = 0;
}

// 查询当前是否有按键被按下（实时电平）
uint8_t Key_IsAnyPressed(void)
{
    for (uint8_t i = 0; i < KEY_NUM; i++) {
        if (Key_GetLevel(i)) return i;
    }
    return 0xFF;
}

// ---- FreeRTOS 按键扫描任务 ----
// 每 10ms 扫描按键，检测到事件后通过队列发送给 TouchGFX
void Key_Task(void *argument)
{
    KeyEvent_t evt;
    
    uint32_t wake_time = osKernelGetTickCount();
    for (;;) {
        wake_time += 10;
        osDelayUntil(wake_time);
        
        Key_Scan();
        
        // 检查所有按键的按下事件和松开事件，通过队列发送
        for (uint8_t i = 0; i < KEY_NUM; i++) {
            if (Key_GetPressEvent(i)) {
                evt.key_id = i;
                evt.type = 1;  // 按下事件
                osMessageQueuePut(keyEventQueue, &evt, 0, 0);
                Key_ClearPressEvent(i);
            }
            if (Key_GetEvent(i)) {
                evt.key_id = i;
                evt.type = 0;  // 松开事件（单击）
                osMessageQueuePut(keyEventQueue, &evt, 0, 0);
                Key_ClearEvent(i);
            }
        }
    }
}
