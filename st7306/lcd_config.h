// <<< Use Configuration Wizard in Context Menu >>>
#ifndef LCD_CONFIG_H
#define LCD_CONFIG_H

#include "stm32g4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// <h> LCD Hardware Configuration

// <o> Display Width(change in lcd.h) <1-4096>
// #define LCD_W 300

// <o> Display Height(change in lcd.h) <1-4096>
// #define LCD_H 400

// <o> Rotation <0=> 0° <1=> 90° <2=> 180° <3=> 270°
#define USE_HORIZONTAL 0

// <h> SPI Pin Definitions
// <o> CS Port <1=>GPIOA <2=>GPIOB <3=>GPIOC <4=>GPIOD <5=>GPIOE <6=>GPIOF
#define LCD_CS_PORT_NUM 4
// <o> CS Pin <0-15>
#define LCD_CS_PIN 10

// <o> DC/RS Port <1=>GPIOA <2=>GPIOB <3=>GPIOC <4=>GPIOD <5=>GPIOE <6=>GPIOF
#define LCD_DC_PORT_NUM 4
// <o> DC/RS Pin <0-15>
#define LCD_DC_PIN 9

// <o> RST Port <1=>GPIOA <2=>GPIOB <3=>GPIOC <4=>GPIOD <5=>GPIOE <6=>GPIOF
#define LCD_RST_PORT_NUM 4
// <o> RST Pin <0-15>
#define LCD_RST_PIN 8
// </h>

// </h>

// Convert port number to GPIO port macro
#if LCD_CS_PORT_NUM == 1
  #define LCD_CS_PORT GPIOA
#elif LCD_CS_PORT_NUM == 2
  #define LCD_CS_PORT GPIOB
#elif LCD_CS_PORT_NUM == 3
  #define LCD_CS_PORT GPIOC
#elif LCD_CS_PORT_NUM == 4
  #define LCD_CS_PORT GPIOD
#elif LCD_CS_PORT_NUM == 5
  #define LCD_CS_PORT GPIOE
#elif LCD_CS_PORT_NUM == 6
  #define LCD_CS_PORT GPIOF
#else
  #error "Invalid LCD_CS_PORT_NUM"
#endif

#if LCD_DC_PORT_NUM == 1
  #define LCD_DC_PORT GPIOA
#elif LCD_DC_PORT_NUM == 2
  #define LCD_DC_PORT GPIOB
#elif LCD_DC_PORT_NUM == 3
  #define LCD_DC_PORT GPIOC
#elif LCD_DC_PORT_NUM == 4
  #define LCD_DC_PORT GPIOD
#elif LCD_DC_PORT_NUM == 5
  #define LCD_DC_PORT GPIOE
#elif LCD_DC_PORT_NUM == 6
  #define LCD_DC_PORT GPIOF
#else
  #error "Invalid LCD_DC_PORT_NUM"
#endif

#if LCD_RST_PORT_NUM == 1
  #define LCD_RST_PORT GPIOA
#elif LCD_RST_PORT_NUM == 2
  #define LCD_RST_PORT GPIOB
#elif LCD_RST_PORT_NUM == 3
  #define LCD_RST_PORT GPIOC
#elif LCD_RST_PORT_NUM == 4
  #define LCD_RST_PORT GPIOD
#elif LCD_RST_PORT_NUM == 5
  #define LCD_RST_PORT GPIOE
#elif LCD_RST_PORT_NUM == 6
  #define LCD_RST_PORT GPIOF
#else
  #error "Invalid LCD_RST_PORT_NUM"
#endif


// Convert pin number to bit mask (same as GPIO_PIN_x)
#define LCD_CS_PIN_MASK   (1U << LCD_CS_PIN)
#define LCD_DC_PIN_MASK   (1U << LCD_DC_PIN)
#define LCD_RST_PIN_MASK  (1U << LCD_RST_PIN)

// Generate pin operation macros (using bit masks)
#define CS_High     (LCD_CS_PORT->BSRR = LCD_CS_PIN_MASK)                     //片选拉高
#define CS_Low      (LCD_CS_PORT->BSRR = (uint32_t)(LCD_CS_PIN_MASK << 16))   //片选拉低
#define DC_High     (LCD_DC_PORT->BSRR = LCD_DC_PIN_MASK)                     //数据/命令选择拉高（数据模式）
#define DC_Low      (LCD_DC_PORT->BSRR = (uint32_t)(LCD_DC_PIN_MASK << 16))   //数据/命令选择拉低（命令模式）
#define RES_High    (LCD_RST_PORT->BSRR = LCD_RST_PIN_MASK)                   //复位拉高
#define RES_Low     (LCD_RST_PORT->BSRR = (uint32_t)(LCD_RST_PIN_MASK << 16)) //复位拉低


#ifdef __cplusplus
}
#endif

#endif // LCD_CONFIG_H