#include "stm32g4xx_hal.h"
#include "driver_drv8803.h"


#ifdef USE_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#endif

/**
 * @brief 配置两个 DRV8803 芯片的初始状态
 * @note  所有 GPIO 模式（输入/输出/上拉）必须在 CubeMX 中正确配置。
 *        本函数仅设置初始输出电平，不调用 HAL_GPIO_Init，避免与 MX_GPIO_Init 冲突。
 */
HAL_StatusTypeDef DRV8803_Dual_Config(void)
{
    // U12 (12V) 初始状态
    HAL_GPIO_WritePin(DRV1_EN_PORT, DRV1_EN_PIN, GPIO_PIN_SET);      // 禁用
    HAL_GPIO_WritePin(DRV1_RESET_PORT, DRV1_RESET_PIN, GPIO_PIN_SET); // 正常工作时必须 将 RESET 拉高，只有需要复位才拉低。
    HAL_GPIO_WritePin(DRV1_IN1_PORT, DRV1_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DRV1_IN2_PORT, DRV1_IN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DRV1_IN3_PORT, DRV1_IN3_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DRV1_IN4_PORT, DRV1_IN4_PIN, GPIO_PIN_RESET);

    // U13 (24V) 初始状态
    HAL_GPIO_WritePin(DRV2_EN_PORT, DRV2_EN_PIN, GPIO_PIN_SET);      // 禁用
    HAL_GPIO_WritePin(DRV2_RESET_PORT, DRV2_RESET_PIN, GPIO_PIN_SET); // 正常工作时必须 将 RESET 拉高，只有需要复位才拉低。
    HAL_GPIO_WritePin(DRV2_IN1_PORT, DRV2_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DRV2_IN2_PORT, DRV2_IN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DRV2_IN3_PORT, DRV2_IN3_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DRV2_IN4_PORT, DRV2_IN4_PIN, GPIO_PIN_RESET);

    // PWM 控制线初始状态（低电平）
    HAL_GPIO_WritePin(PWM_12V_C1_PORT, PWM_12V_C1_PIN, GPIO_PIN_RESET);
//    HAL_GPIO_WritePin(PWM_12V_C2_PORT, PWM_12V_C2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWM_24V_C1_PORT, PWM_24V_C1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWM_24V_C2_PORT, PWM_24V_C2_PIN, GPIO_PIN_RESET);

    return HAL_OK;
}

void DRV8803_EnableChip(uint8_t chip_id, bool enable)
{
    if (chip_id == 1) {
        HAL_GPIO_WritePin(DRV1_EN_PORT, DRV1_EN_PIN, enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
    } else if (chip_id == 2) {
        HAL_GPIO_WritePin(DRV2_EN_PORT, DRV2_EN_PIN, enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}

void DRV8803_SetGlobalChannel(GlobalChannel_t ch, bool state)
{
    GPIO_PinState pinState = state ? GPIO_PIN_SET : GPIO_PIN_RESET;

    switch (ch) {
        case CH1: HAL_GPIO_WritePin(DRV1_IN1_PORT, DRV1_IN1_PIN, pinState); break;
        case CH2: HAL_GPIO_WritePin(DRV1_IN2_PORT, DRV1_IN2_PIN, pinState); break;
        case CH3: HAL_GPIO_WritePin(DRV1_IN3_PORT, DRV1_IN3_PIN, pinState); break;
        case CH4: HAL_GPIO_WritePin(DRV1_IN4_PORT, DRV1_IN4_PIN, pinState); break;
        case CH5: HAL_GPIO_WritePin(DRV2_IN1_PORT, DRV2_IN1_PIN, pinState); break;
        case CH6: HAL_GPIO_WritePin(DRV2_IN2_PORT, DRV2_IN2_PIN, pinState); break;
        case CH7: HAL_GPIO_WritePin(DRV2_IN3_PORT, DRV2_IN3_PIN, pinState); break;
        case CH8: HAL_GPIO_WritePin(DRV2_IN4_PORT, DRV2_IN4_PIN, pinState); break;
        default: break;
    }
}

void DRV8803_SetChipChannels(uint8_t chip_id, uint8_t channel_mask)
{
    if (chip_id == 1) {
        DRV8803_SetGlobalChannel(CH1, (channel_mask & 0x01));
        DRV8803_SetGlobalChannel(CH2, (channel_mask & 0x02));
        DRV8803_SetGlobalChannel(CH3, (channel_mask & 0x04));
        DRV8803_SetGlobalChannel(CH4, (channel_mask & 0x08));
    } else if (chip_id == 2) {
        DRV8803_SetGlobalChannel(CH5, (channel_mask & 0x01));
        DRV8803_SetGlobalChannel(CH6, (channel_mask & 0x02));
        DRV8803_SetGlobalChannel(CH7, (channel_mask & 0x04));
        DRV8803_SetGlobalChannel(CH8, (channel_mask & 0x08));
    }
}

bool DRV8803_IsChipFault(uint8_t chip_id)
{
    if (chip_id == 1) {
        return (HAL_GPIO_ReadPin(DRV1_FAULT_PORT, DRV1_FAULT_PIN) == GPIO_PIN_RESET);
    } else if (chip_id == 2) {
        return (HAL_GPIO_ReadPin(DRV2_FAULT_PORT, DRV2_FAULT_PIN) == GPIO_PIN_RESET);
    }
    return false;
}

void DRV8803_TriggerChipReset(uint8_t chip_id)
{
    GPIO_TypeDef* resetPort;
    uint16_t resetPin;

    if (chip_id == 1) {
        resetPort = DRV1_RESET_PORT;
        resetPin  = DRV1_RESET_PIN;
    } else if (chip_id == 2) {
        resetPort = DRV2_RESET_PORT;
        resetPin  = DRV2_RESET_PIN;
    } else {
        return;
    }

    // 复位时序：高脉冲 >20us
    HAL_GPIO_WritePin(resetPort, resetPin, GPIO_PIN_RESET);
    HAL_Delay(1);   // 1ms，远大于 20us
    HAL_GPIO_WritePin(resetPort, resetPin, GPIO_PIN_SET);
    HAL_Delay(1);   // 稳定等待
}

/**
 * @brief 故障恢复处理（适用于 FreeRTOS 任务，无阻塞延时）
 */
void DRV8803_HandleFault_RTOS(uint8_t chip_id)
{
    // 1. 立即禁用输出
    DRV8803_EnableChip(chip_id, false);

    // 2. 等待自动重试时间（t_RETRY=1.2ms），使用 FreeRTOS 延时
    #ifdef USE_FREERTOS
    vTaskDelay(pdMS_TO_TICKS(5));
    #else
    HAL_Delay(5);
    #endif

    // 3. 执行硬件复位
    DRV8803_TriggerChipReset(chip_id);

    // 4. 检查故障是否清除
    if (!DRV8803_IsChipFault(chip_id)) {
        // 可选择重新使能，由上层决定
        // DRV8803_EnableChip(chip_id, true);
    }
}