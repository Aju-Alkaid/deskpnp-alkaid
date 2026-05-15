#ifndef __KEY_H__
#define __KEY_H__

//#include "sys.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ======================== 鐢ㄦ埛閰嶇疆鍖哄煙 ========================
#define KEY_NUM         5   // 鎸夐敭鏁伴噺

// 鎸夐敭寮曡剼瀹氫箟锛堟牴鎹疄闄呮帴绾匡級
// KEY1
#define KEY1_GPIO_PORT  GPIOC
#define KEY1_GPIO_PIN   GPIO_PIN_6
#define KEY1_ACTIVE_LEVEL  GPIO_PIN_RESET   // 鎸変笅涓轰綆鐢靛钩

// KEY2
#define KEY2_GPIO_PORT  GPIOC
#define KEY2_GPIO_PIN   GPIO_PIN_7
#define KEY2_ACTIVE_LEVEL  GPIO_PIN_RESET

// CW (椤烘椂閽堟棆杞?
#define KEY_CW_GPIO_PORT  GPIOC
#define KEY_CW_GPIO_PIN   GPIO_PIN_8
#define KEY_CW_ACTIVE_LEVEL  GPIO_PIN_RESET

// CCW (閫嗘椂閽堟棆杞?
#define KEY_CCW_GPIO_PORT GPIOA
#define KEY_CCW_GPIO_PIN  GPIO_PIN_8
#define KEY_CCW_ACTIVE_LEVEL GPIO_PIN_RESET

// PUSH (鎸夊帇)
#define KEY_PUSH_GPIO_PORT GPIOC
#define KEY_PUSH_GPIO_PIN  GPIO_PIN_9
#define KEY_PUSH_ACTIVE_LEVEL GPIO_PIN_RESET

// // 寮€鍙戞澘Key
// #define KEY_dev_GPIO_PORT GPIOD
// #define KEY_dev_GPIO_PIN  GPIO_PIN_15
// #define KEY_dev_ACTIVE_LEVEL GPIO_PIN_RESET

// 娑堟姈鏃堕棿锛堟绉掞級锛屽缓璁?0~30ms
#define KEY_DEBOUNCE_MS    20

// // 鎸夐敭杩斿洖鍊煎畾涔夛紙涓庡師鏉ヤ繚鎸佷竴鑷达級
#define key1    0   // KEY1
#define key2    1   // KEY2
#define down    2   // CW椤烘椂閽?/down
#define up      3   // CCW閫嗘椂閽?/wp
#define push    4   // PUSH


// ======================== API 鍑芥暟 ========================
void Key_Init(void);
void Key_Scan(void);                // 闇€鍦ㄤ富寰幆涓懆鏈熻皟鐢紙寤鸿闂撮殧10ms锛?
uint8_t Key_GetEvent(uint8_t key_id); // 杩斿洖鍗曞嚮浜嬩欢(1) 鎴?0
uint8_t Key_IsAnyPressed(void);        // 杩斿洖褰撳墠琚寜涓嬫寜閿殑閿€硷紙瀹炴椂鐢靛钩锛?
void Key_ClearEvent(uint8_t key_id);   // 娓呴櫎浜嬩欢

#ifdef __cplusplus
}
#endif

#endif