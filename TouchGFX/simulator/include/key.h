#ifndef TOUCHGFX_SIM_KEY_H
#define TOUCHGFX_SIM_KEY_H

#ifdef SIMULATOR

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KEY_KEY1
#define KEY_KEY1    0
#endif
#ifndef KEY_KEY2
#define KEY_KEY2    1
#endif
#ifndef KEY_DOWN
#define KEY_DOWN    2
#endif
#ifndef KEY_UP
#define KEY_UP      3
#endif
#ifndef KEY_PUSH
#define KEY_PUSH    4
#endif

static inline void Key_Init(void) {}
static inline void Key_Scan(void) {}
static inline uint8_t Key_GetEvent(uint8_t key_id) { (void)key_id; return 0; }
static inline uint8_t Key_GetPressEvent(uint8_t key_id) { (void)key_id; return 0; }
static inline uint8_t Key_IsAnyPressed(void) { return 0; }
static inline void Key_ClearEvent(uint8_t key_id) { (void)key_id; }
static inline void Key_ClearPressEvent(uint8_t key_id) { (void)key_id; }

#ifdef __cplusplus
}
#endif

#endif /* SIMULATOR */
#endif /* TOUCHGFX_SIM_KEY_H */
