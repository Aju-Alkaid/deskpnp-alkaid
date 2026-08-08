/**
 * @file    driver_esp32.c
 * @brief   ESP32-C3 SPI slave driver for STM32 master.
 * @note    SPI4 master, Mode 0, CS=PE3, IRQ=PC13.
 *          v3.1 has no hardware reset wire.
 */

#include "driver_esp32.h"
#include "app_test.h"
#include <string.h>

extern SPI_HandleTypeDef hspi4;

volatile uint8_t esp32_irq_flag = 0;

void ESP_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(SPI4_CS_GPIO_Port, SPI4_CS_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Pin   = ESP_IRQ_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(ESP_IRQ_GPIO_Port, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    esp32_irq_flag = 0;
}

int ESP_SPI_Transfer(uint8_t *tx_buf, uint8_t *rx_buf)
{
    HAL_StatusTypeDef status;
    uint32_t start_tick;

    if (esp_spi_mutex != NULL) {
        osMutexAcquire(esp_spi_mutex, osWaitForever);
    }

    HAL_GPIO_WritePin(SPI4_CS_GPIO_Port, SPI4_CS_Pin, GPIO_PIN_SET);
    osDelay(1);

    start_tick = HAL_GetTick();
    HAL_GPIO_WritePin(SPI4_CS_GPIO_Port, SPI4_CS_Pin, GPIO_PIN_RESET);

    status = HAL_SPI_TransmitReceive(&hspi4, tx_buf, rx_buf,
                                     ESP_PACKET_SIZE, 100);

    HAL_GPIO_WritePin(SPI4_CS_GPIO_Port, SPI4_CS_Pin, GPIO_PIN_SET);

    if (status != HAL_OK) {
        if (esp_spi_mutex != NULL) osMutexRelease(esp_spi_mutex);
        return status;
    }

    if ((HAL_GetTick() - start_tick) >= 100U) {
        if (esp_spi_mutex != NULL) osMutexRelease(esp_spi_mutex);
        return HAL_TIMEOUT;
    }

    {
        uint32_t elapsed = HAL_GetTick() - start_tick;
        if (elapsed >= 5U) {
            PrintDebug("[ESP] SPI4 CS low took %u ms\r\n",
                       (unsigned)elapsed);
        }
    }

    /* Keep CS high long enough for the ESP32 slave to close the frame. */
    osDelay(2);

    if (esp_spi_mutex != NULL) osMutexRelease(esp_spi_mutex);
    return HAL_OK;
}
