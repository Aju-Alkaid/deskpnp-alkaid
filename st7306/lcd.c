#include "lcd.h"
#include <string.h>

extern SPI_HandleTypeDef hspi2;
static SPI_HandleTypeDef *hspi_lcd = NULL;

/* ?? SPI ??/??????????FreeRTOS ??? */
static void Write_Cmd(uint8_t cmd) {
    CS_Low;
    DC_Low;
    HAL_SPI_Transmit(hspi_lcd, &cmd, 1, HAL_MAX_DELAY);
    CS_High;
}

static void Write_Data(uint8_t data) {
    CS_Low;
    DC_High;
    HAL_SPI_Transmit(hspi_lcd, &data, 1, HAL_MAX_DELAY);
    CS_High;
}

static void Write_Data_Bulk(const uint8_t *data, uint16_t len) {
    CS_Low;
    DC_High;
    HAL_SPI_Transmit(hspi_lcd, (uint8_t*)data, len, HAL_MAX_DELAY);
    CS_High;
}

/* ???????8 ???? */
void Set_Address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    uint16_t hw_x1 = HARDWARE_COL_START + x1;
    uint16_t hw_x2 = HARDWARE_COL_START + x2;
    uint8_t col_start = hw_x1 / 12;
    uint8_t col_end   = hw_x2 / 12;

    uint16_t hw_y1 = HARDWARE_ROW_START + y1;
    uint16_t hw_y2 = HARDWARE_ROW_START + y2;
    uint8_t row_start = hw_y1 / 2;
    uint8_t row_end   = hw_y2 / 2;

    Write_Cmd(0x2A);   // CASET
    Write_Data(col_start);
    Write_Data(col_end);

    Write_Cmd(0x2B);   // RASET
    Write_Data(row_start);
    Write_Data(row_end);

    Write_Cmd(0x2C);   // RAMWR
}

/* ???? */
static void ST7306_HardReset(void) {
    RES_High; HAL_Delay(10);
    RES_Low;  HAL_Delay(20);
    RES_High; HAL_Delay(120);
}

/*
 * ????????????????
 * 0xC0 ??????????????????
 * 0xC1	???????
 * 0xC4	???????  ????????????????????
*/
static void Power_Voltage_Setting(void) {
    Write_Cmd(0xD1); Write_Data(0x01);  // Booster Enable
    Write_Cmd(0xC0); Write_Data(0x10); Write_Data(0x0E);
    Write_Cmd(0xC1); Write_Data(0x5A); Write_Data(0x5A); Write_Data(0x5A); Write_Data(0x5A);
    Write_Cmd(0xC2); Write_Data(0x63); Write_Data(0x63); Write_Data(0x63); Write_Data(0x63);
    Write_Cmd(0xC4); Write_Data(0x64); Write_Data(0x64); Write_Data(0x64); Write_Data(0x64);
    Write_Cmd(0xC5); Write_Data(0x46); Write_Data(0x46); Write_Data(0x46); Write_Data(0x46);
}

/* ????? */
static void ST7306_InitSequence(void) {
    Write_Cmd(0xD6); Write_Data(0x17); Write_Data(0x02);
    Power_Voltage_Setting();

    Write_Cmd(0xB2); Write_Data(0x05);

    Write_Cmd(0xB3); Write_Data(0xE5); Write_Data(0xF6); Write_Data(0x05);
    Write_Data(0x46); Write_Data(0x77); Write_Data(0x77);
    Write_Data(0x77); Write_Data(0x77); Write_Data(0x76); Write_Data(0x45);

    Write_Cmd(0xB4); Write_Data(0x05); Write_Data(0x46); Write_Data(0x77);
    Write_Data(0x77); Write_Data(0x77); Write_Data(0x77); Write_Data(0x76); Write_Data(0x45);

    Write_Cmd(0xB7); Write_Data(0x13);
    Write_Cmd(0xB0); Write_Data(0x64);

    Write_Cmd(0x11); HAL_Delay(120);

    Write_Cmd(0xD8); Write_Data(0xA6); Write_Data(0xE9);
    Write_Cmd(0xC9); Write_Data(0x00);
    Write_Cmd(0x36); Write_Data(0x48);
    Write_Cmd(0x3A); Write_Data(0x11);   // ????
    Write_Cmd(0xB9); Write_Data(0x20);   // Mono
    Write_Cmd(0xB8); Write_Data(0x29);   // ???
    Write_Cmd(0x35); Write_Data(0x00);
    Write_Cmd(0xD0); Write_Data(0x00);
    Write_Cmd(0x38);                     // ?????
    HAL_Delay(150);

    Write_Cmd(0x29);                     // Display ON
    Write_Cmd(0x21);                     // ????
    Write_Cmd(0xBB); Write_Data(0x4F);   // Clear RAM
    HAL_Delay(10);
}

/* ?? API???? LCD */
void ST7306_Init(SPI_HandleTypeDef *hspi) {
    hspi_lcd = hspi;
    CS_High; DC_High; RES_High;
    ST7306_HardReset();
    ST7306_InitSequence();
}

/*
 * ? TouchGFX ? 1bpp ???????? ST7306 ? 4??2? ?????
 * fb: TouchGFX ???????1bpp????????8???
 * ?????????????????????????? display_buffer
 */
void ST7306_Refresh1bpp(const uint8_t *fb) {
    Set_Address(0, 0, LCD_W - 1, LCD_H - 1);

    uint8_t row_buf[LCD_DATA_WIDTH];  // 75 ??/?????????
    uint16_t stride = (LCD_W + 7) / 8; // 38 ??/??1bpp ?????

    for (uint16_t y = 0; y < LCD_H; y += 2) {
        for (uint16_t x = 0; x < LCD_W; x += 4) {
            uint8_t byte_val = 0;
            for (uint8_t dy = 0; dy < 2; dy++) {
                for (uint8_t dx = 0; dx < 4; dx++) {
                    uint16_t px = x + dx;
                    uint16_t py = y + dy;
                    uint16_t src_idx = py * stride + (px / 8);
                    uint8_t  src_bit = 7 - (px % 8);
                    if (fb[src_idx] & (1 << src_bit)) {
                        uint8_t dst_bit = 7 - (dx * 2 + dy);
                        byte_val |= (1 << dst_bit);
                    }
                }
            }
            row_buf[x / 4] = byte_val;
        }
        // ?????????????????? SPI ??
        CS_Low;
        DC_High;
        HAL_SPI_Transmit(hspi_lcd, row_buf, LCD_DATA_WIDTH, HAL_MAX_DELAY);
        CS_High;
    }
}
