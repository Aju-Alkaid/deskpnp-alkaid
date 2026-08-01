/**
 * @file    driver_esp32.c
 * @brief   ESP32-C3 SPI 驱动层
 * @note    SPI4 主机模式, Mode 0 (CPOL=0, CPHA=0)
 *          CS=PE3, RST=PC13, IRQ=PC2 (下降沿中断)
 *          ESP_PACKET_SIZE = 128 字节 (与从机一致)
 */

#include "driver_esp32.h"
#include <string.h>

/* 外部 SPI4 句柄 (spi.c 中定义) */
extern SPI_HandleTypeDef hspi4;

/* ================================================================
 *  IRQ 标志位 (EXTI ISR 置 1, ESP_Task 主循环检查并清零)
 * ================================================================ */
volatile uint8_t esp32_irq_flag = 0;

/*
 * 初始化 ESP32 相关 GPIO (CS/RST/IRQ)
 */
void ESP_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* ---- CS 置高 (空闲) ---- */
    HAL_GPIO_WritePin(SPI4_CS_GPIO_Port, SPI4_CS_Pin, GPIO_PIN_SET);

    /* ---- 释放硬件复位 ---- */
    HAL_GPIO_WritePin(C3RESET_GPIO_Port, C3RESET_Pin, GPIO_PIN_SET);

    /* ---- IRQ 引脚: 输入 + 上拉 + EXTI 下降沿中断 ---- */
    GPIO_InitStruct.Pin   = ESP_IRQ_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(ESP_IRQ_GPIO_Port, &GPIO_InitStruct);

    /* EXTI2 中断优先级 6 (低于 SPI/CAN 的 5, 高于 TouchGFX 的 7+) */
    HAL_NVIC_SetPriority(EXTI2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI2_IRQn);
}

/*
 * 硬件复位 ESP32-C3
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

/*
 * SPI4 全双工收发 128 字节 (阻塞式)
 *
 * 文档要求 CS 低电平超时 100ms 保护，当前依赖 HAL_SPI_TransmitReceive
 * 自身的 timeout 参数 (100ms)，不做额外的 HAL_GetTick 监控。
 * 实测 128 字节 @ 2.66MHz 理论耗时 ~0.4ms，100ms 余量充足。
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