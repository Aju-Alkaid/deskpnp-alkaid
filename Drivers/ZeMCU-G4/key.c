#include "key.h"
#include "stm32g4xx_hal.h"
#include <string.h>

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

// 按键状态结构体（简化：只保留按下标志、事件标志、消抖计数）
typedef struct {
    uint8_t pressed;        // 当前是否处于“已按下并确认”状态
    uint8_t event;          // 0=无事件, 1=单击事件
    uint8_t debounce_cnt;   // 消抖计数器
} KeyStatus_t;

static KeyStatus_t key_status[KEY_NUM];

// 获取当前按键电平（按下返回1，未按下返回0）
static uint8_t Key_GetLevel(uint8_t key_id)
{
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(key_hw[key_id].port, key_hw[key_id].pin);
    return (pin_state == key_hw[key_id].active_level) ? 1 : 0;
}

// 获取系统毫秒数（复用HAL库）
static uint32_t Key_GetTick(void)
{
    return HAL_GetTick();
}

// 初始化GPIO
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

// 按键扫描函数（建议每10ms调用一次）
void Key_Scan(void)
{
    static const uint8_t debounce_samples = KEY_DEBOUNCE_MS / 10; // 需要的连续稳定采样次数
    if (debounce_samples == 0) return;

    for (uint8_t i = 0; i < KEY_NUM; i++) {
        uint8_t current_level = Key_GetLevel(i);
        KeyStatus_t *ks = &key_status[i];

        // 边沿检测 + 消抖
        if (current_level) { // 当前读到按下
            if (!ks->pressed) {
                // 尚未确认按下，进行按下消抖
                ks->debounce_cnt++;
                if (ks->debounce_cnt >= debounce_samples) {
                    ks->pressed = 1;          // 确认按下
                    ks->debounce_cnt = 0;
                }
            } else {
                // 已确认按下，清空消抖计数（防止释放时残留）
                ks->debounce_cnt = 0;
            }
        } else { // 当前读到释放
            if (ks->pressed) {
                // 已确认按下，现在检测释放 → 进行释放消抖
                ks->debounce_cnt++;
                if (ks->debounce_cnt >= debounce_samples) {
                    // 确认释放，产生一次单击事件
                    if (ks->event == 0) {
                        ks->event = 1;
                    }
                    ks->pressed = 0;
                    ks->debounce_cnt = 0;
                }
            } else {
                // 空闲状态，清空消抖计数
                ks->debounce_cnt = 0;
            }
        }
    }
}

// 获取按键事件（非阻塞）
uint8_t Key_GetEvent(uint8_t key_id)
{
    if (key_id >= KEY_NUM) return 0;
    return key_status[key_id].event;
}

// 清除按键事件
void Key_ClearEvent(uint8_t key_id)
{
    if (key_id >= KEY_NUM) return;
    key_status[key_id].event = 0;
}

// 查询当前是否有按键被按下（实时电平，不加消抖，适合需要快速响应的场合）
// 返回值：down, up, confirm, cancel, push, '5' 或 0(无按键)
uint8_t Key_IsAnyPressed(void)
{
    for (uint8_t i = 0; i < KEY_NUM; i++) {
        if (Key_GetLevel(i)) return i; // 返回按键ID
    }
    return 0xFF; // 表示无按键
}
