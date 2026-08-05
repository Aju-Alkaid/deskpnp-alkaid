#ifndef __DRIVER_ESP32_H
#define __DRIVER_ESP32_H

#include <stdint.h>
#include "spi.h"
#include "main.h"
#include "cmsis_os2.h"

/* Created in app_freertos.c, protects all SPI4 full-duplex transfers. */
extern osMutexId_t esp_spi_mutex;

/* Fixed packet size shared with the ESP32 slave. */
#define ESP_PACKET_SIZE  128

/* ESP32 GPIO13 -> STM32 PC13, falling-edge interrupt. */
#define ESP_IRQ_Pin         GPIO_PIN_13
#define ESP_IRQ_GPIO_Port   GPIOC

/* Set by EXTI ISR, cleared by ESP_Task after scene B completes. */
extern volatile uint8_t esp32_irq_flag;

void ESP_GPIO_Init(void);

static inline uint8_t ESP_CheckIRQ(void)
{
    return (HAL_GPIO_ReadPin(ESP_IRQ_GPIO_Port, ESP_IRQ_Pin) == GPIO_PIN_SET) ? 1 : 0;
}

int ESP_SPI_Transfer(uint8_t *tx_buf, uint8_t *rx_buf);

#endif
