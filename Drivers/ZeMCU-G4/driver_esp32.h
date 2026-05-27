#ifndef __DRIVER_ESP32_H
#define __DRIVER_ESP32_H

#include <stdint.h>
#include "spi.h"
#include "main.h"

/* SPI 数据包固定长度 (与 ESP32 端一致) */
#define ESP_PACKET_SIZE  128

/**
 * @brief  初始化 ESP32 相关 GPIO
 * @note   设置 SPI4_CS (PE3) 为高电平 (空闲)
 *         设置 C3RESET (PC13) 为高电平 (释放复位)
 *         CubeMX 生成的 MX_GPIO_Init 中这两个引脚初始为低，
 *         本函数在上电后调用以恢复正常电平。
 */
void ESP_GPIO_Init(void);

/**
 * @brief  硬件复位 ESP32-C3
 * @note   时序: C3RESET=LOW 100ms -> C3RESET=HIGH -> 等待 500ms
 *         应在任务上下文中调用 (含 HAL_Delay)
 */
void ESP_HardReset(void);

/**
 * @brief  SPI4 全双工收发 128 字节 (阻塞式)
 * @param  tx_buf  发送缓冲区 (128 字节)
 * @param  rx_buf  接收缓冲区 (128 字节，可与 tx_buf 为同一数组)
 * @retval HAL_OK / HAL_ERROR / HAL_TIMEOUT
 * @note   内部控制 CS 拉低/拉高
 *         超时 100ms (SPI 速率约 2.66MHz，128 字节理论耗时 ~0.4ms)
 *         禁止在 ISR 中调用
 */
int ESP_SPI_Transfer(uint8_t *tx_buf, uint8_t *rx_buf);

#endif
