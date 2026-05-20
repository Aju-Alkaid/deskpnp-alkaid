#ifndef __LCD_H
#define __LCD_H

#include "main.h"
#include "lcd_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 颜色定义 */
#define BLACK   0
#define WHITE   1

/* 屏幕物理尺寸（请根据实际修改） */
#define HARDWARE_COL_START   216   // 硬件列起始偏移（像素）//硬件实际偏移值216，即有效列范围是 216~515（共 300 列）
#define HARDWARE_ROW_START   0    // 硬件行起始偏移（像素），如果无偏移保持0
#define LCD_W                300  // 逻辑宽度（像素）
#define LCD_H                400  // 逻辑高度（像素）

/* 缓冲区尺寸：每字节管理 4 列 × 2 行 */
#define LCD_DATA_WIDTH   (LCD_W / 4)      // 150
#define LCD_DATA_HEIGHT  (LCD_H / 2)      // 200
#define LCD_BUFFER_SIZE  (LCD_DATA_WIDTH * LCD_DATA_HEIGHT)  // 30000 字节

/* 外部接口 */
void ST7306_Init(SPI_HandleTypeDef *hspi);
void Set_Address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void ST7306_Clear(void);
void ST7306_Fill(uint8_t color);
void ST7306_Refresh(void);
void ST7306_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color);
void LCD_ShowChar(uint16_t x, uint16_t y, char ch);
void LCD_ShowString(uint16_t x, uint16_t y, const char *str);
extern uint8_t display_buffer[LCD_BUFFER_SIZE];

#ifdef __cplusplus
}
#endif

#endif