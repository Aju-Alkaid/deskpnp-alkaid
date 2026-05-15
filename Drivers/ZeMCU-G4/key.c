#include "key.h"
#include "stm32g4xx_hal.h"
#include <string.h>

// 鎸夐敭纭欢閰嶇疆琛?
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

// 鎸夐敭鐘舵€佺粨鏋勪綋锛堢畝鍖栵細鍙繚鐣欐寜涓嬫爣蹇椼€佷簨浠舵爣蹇椼€佹秷鎶栬鏁帮級
typedef struct {
    uint8_t pressed;        // 褰撳墠鏄惁澶勪簬鈥滃凡鎸変笅骞剁‘璁も€濈姸鎬?
    uint8_t event;          // 0=鏃犱簨浠? 1=鍗曞嚮浜嬩欢
    uint8_t debounce_cnt;   // 娑堟姈璁℃暟鍣?
} KeyStatus_t;

static KeyStatus_t key_status[KEY_NUM];

// 鑾峰彇褰撳墠鎸夐敭鐢靛钩锛堟寜涓嬭繑鍥?锛屾湭鎸変笅杩斿洖0锛?
static uint8_t Key_GetLevel(uint8_t key_id)
{
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(key_hw[key_id].port, key_hw[key_id].pin);
    return (pin_state == key_hw[key_id].active_level) ? 1 : 0;
}

// 鑾峰彇绯荤粺姣鏁帮紙澶嶇敤HAL搴擄級
static uint32_t Key_GetTick(void)
{
    return HAL_GetTick();
}

// 鍒濆鍖朑PIO
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

// 鎸夐敭鎵弿鍑芥暟锛堝缓璁瘡10ms璋冪敤涓€娆★級
void Key_Scan(void)
{
    static const uint8_t debounce_samples = KEY_DEBOUNCE_MS / 10; // 闇€瑕佺殑杩炵画绋冲畾閲囨牱娆℃暟
    if (debounce_samples == 0) return;

    for (uint8_t i = 0; i < KEY_NUM; i++) {
        uint8_t current_level = Key_GetLevel(i);
        KeyStatus_t *ks = &key_status[i];

        // 杈规部妫€娴?+ 娑堟姈
        if (current_level) { // 褰撳墠璇诲埌鎸変笅
            if (!ks->pressed) {
                // 灏氭湭纭鎸変笅锛岃繘琛屾寜涓嬫秷鎶?
                ks->debounce_cnt++;
                if (ks->debounce_cnt >= debounce_samples) {
                    ks->pressed = 1;          // 纭鎸変笅
                    ks->debounce_cnt = 0;
                }
            } else {
                // 宸茬‘璁ゆ寜涓嬶紝娓呯┖娑堟姈璁℃暟锛堥槻姝㈤噴鏀炬椂娈嬬暀锛?
                ks->debounce_cnt = 0;
            }
        } else { // 褰撳墠璇诲埌閲婃斁
            if (ks->pressed) {
                // 宸茬‘璁ゆ寜涓嬶紝鐜板湪妫€娴嬮噴鏀?鈫?杩涜閲婃斁娑堟姈
                ks->debounce_cnt++;
                if (ks->debounce_cnt >= debounce_samples) {
                    // 纭閲婃斁锛屼骇鐢熶竴娆″崟鍑讳簨浠?
                    if (ks->event == 0) {
                        ks->event = 1;
                    }
                    ks->pressed = 0;
                    ks->debounce_cnt = 0;
                }
            } else {
                // 绌洪棽鐘舵€侊紝娓呯┖娑堟姈璁℃暟
                ks->debounce_cnt = 0;
            }
        }
    }
}

// 鑾峰彇鎸夐敭浜嬩欢锛堥潪闃诲锛?
uint8_t Key_GetEvent(uint8_t key_id)
{
    if (key_id >= KEY_NUM) return 0;
    return key_status[key_id].event;
}

// 娓呴櫎鎸夐敭浜嬩欢
void Key_ClearEvent(uint8_t key_id)
{
    if (key_id >= KEY_NUM) return;
    key_status[key_id].event = 0;
}

// 鏌ヨ褰撳墠鏄惁鏈夋寜閿鎸変笅锛堝疄鏃剁數骞筹紝涓嶅姞娑堟姈锛岄€傚悎闇€瑕佸揩閫熷搷搴旂殑鍦哄悎锛?
// 杩斿洖鍊硷細down, up, confirm, cancel, push, '5' 鎴?0(鏃犳寜閿?
uint8_t Key_IsAnyPressed(void)
{
    for (uint8_t i = 0; i < KEY_NUM; i++) {
        if (Key_GetLevel(i)) return i; // 杩斿洖鎸夐敭ID
    }
    return 0xFF; // 琛ㄧず鏃犳寜閿?
}
