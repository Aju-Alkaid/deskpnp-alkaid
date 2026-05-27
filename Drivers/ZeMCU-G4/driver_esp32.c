/**
 * @file    driver_esp32.c
 * @brief   ESP32-C3 SPI 驱动层
 * @note    SPI4 主机模式, Mode 0 (CPOL=0, CPHA=0)
 *          CS=PE3, RST=PC13
 *          ESP_PACKET_SIZE = 128 字节 (与从机一致)
 */

#include "driver_esp32.h"
#include <string.h>

/* 外部 SPI4 句柄 (spi.c 中定义) */
extern SPI_HandleTypeDef hspi4;

/**
 * @brief  初始化 ESP32 相关 GPIO
 */
void ESP_GPIO_Init(void)
{
    /* CS 置高 (空闲) */
    HAL_GPIO_WritePin(SPI4_CS_GPIO_Port, SPI4_CS_Pin, GPIO_PIN_SET);

    /* 释放硬件复位 */
    HAL_GPIO_WritePin(C3RESET_GPIO_Port, C3RESET_Pin, GPIO_PIN_SET);
}

/**
 * @brief  硬件复位 ESP32-C3
 */
void ESP_HardReset(void)
{
    /* 拉低复位 100ms */
    HAL_GPIO_WritePin(C3RESET_GPIO_Port, C3RESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);

    /* 释放复位 */
    HAL_GPIO_WritePin(C3RESET_GPIO_Port, C3RESET_Pin, GPIO_PIN_SET);

    /* 等待 ESP32 启动 (约需 300~500ms) */
    HAL_Delay(500);
}

/**
 * @brief  SPI4 全双工收发 128 字节 (阻塞式)
 */
int ESP_SPI_Transfer(uint8_t *tx_buf, uint8_t *rx_buf)
{
    HAL_StatusTypeDef status;

    /* 拉低 CS 开始传输 */
    HAL_GPIO_WritePin(SPI4_CS_GPIO_Port, SPI4_CS_Pin, GPIO_PIN_RESET);

    /* 全双工收发 128 字节，超时 100ms */
    status = HAL_SPI_TransmitReceive(&hspi4, tx_buf, rx_buf,
                                     ESP_PACKET_SIZE, 100);

    /* 拉高 CS 结束传输 */
    HAL_GPIO_WritePin(SPI4_CS_GPIO_Port, SPI4_CS_Pin, GPIO_PIN_SET);

    return (status == HAL_OK) ? HAL_OK : status;
}
