#include "stm32g4xx_hal.h"
#include "driver_drv8803.h"

#ifdef USE_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#endif

// ==================== 8 个物理输出端口实例 ====================
const PowerPort_t Port_12VO1 = {
    .num_pins = 1,
    .pins = {
        { .port = GPIOE, .pin = GPIO_PIN_11 }
    }
};

const PowerPort_t Port_12VO2 = {
    .num_pins = 1,
    .pins = {
        { .port = GPIOE, .pin = GPIO_PIN_12 }
    }
};

const PowerPort_t Port_12VO3 = {
    .num_pins = 2,
    .pins = {
        { .port = GPIOE, .pin = GPIO_PIN_13 },   // 开关
        { .port = GPIOE, .pin = GPIO_PIN_8  }    // PWM
    }
};

const PowerPort_t Port_12VO4 = {
    .num_pins = 2,
    .pins = {
        { .port = GPIOE, .pin = GPIO_PIN_14 },   // 开关
        { .port = GPIOB, .pin = GPIO_PIN_10 }    // PWM
    }
};

const PowerPort_t Port_24VO1 = {
    .num_pins = 1,
    .pins = {
        { .port = GPIOA, .pin = GPIO_PIN_6 }
    }
};

const PowerPort_t Port_24VO2 = {
    .num_pins = 1,
    .pins = {
        { .port = GPIOA, .pin = GPIO_PIN_7 }
    }
};

const PowerPort_t Port_24VO3 = {
    .num_pins = 2,
    .pins = {
        { .port = GPIOC, .pin = GPIO_PIN_4 },    // 开关
        { .port = GPIOB, .pin = GPIO_PIN_1 }     // PWM
    }
};

const PowerPort_t Port_24VO4 = {
    .num_pins = 1,
    .pins = {
        { .port = GPIOC, .pin = GPIO_PIN_5 },    // 开关  (PB2 已让给 KTH7823 编码器)
    }
};

// ==================== 所有端口的列表（用于批量初始化） ====================
static const PowerPort_t * const g_all_ports[] = {
    &Port_12VO1, &Port_12VO2, &Port_12VO3, &Port_12VO4,
    &Port_24VO1, &Port_24VO2, &Port_24VO3, &Port_24VO4
};

/**
 * @brief 初始化所有 DRV8803 输出端口和芯片控制引脚
 * @note  所有 GPIO 模式必须在 CubeMX 中配置。本函数仅设置初始输出电平。
 */
HAL_StatusTypeDef DRV8803_Init(void)
{
    // 所有端口初始化为低电平（12V/24V 统一：HIGH=导通，LOW=断开）
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < g_all_ports[i]->num_pins; j++) {
            HAL_GPIO_WritePin(g_all_ports[i]->pins[j].port,
                              g_all_ports[i]->pins[j].pin,
                              GPIO_PIN_RESET);
        }
    }

    // U12 (12V) 芯片控制引脚
    HAL_GPIO_WritePin(DRV1_EN_PORT, DRV1_EN_PIN, GPIO_PIN_SET);        // 禁用
    HAL_GPIO_WritePin(DRV1_RESET_PORT, DRV1_RESET_PIN, GPIO_PIN_RESET); // 正常工作

    // U13 (24V) 芯片控制引脚
    HAL_GPIO_WritePin(DRV2_EN_PORT, DRV2_EN_PIN, GPIO_PIN_SET);        // 禁用
    HAL_GPIO_WritePin(DRV2_RESET_PORT, DRV2_RESET_PIN, GPIO_PIN_RESET); // 正常工作

    return HAL_OK;
}

/**
 * @brief 控制单个输出端口的开关状态
 * @param port 端口实例指针（如 &Port_12VO1）
 * @param on   true=导通, false=断开
 * @note  仅操作 pins[0]（开关引脚），不影响 PWM 引脚
 */
void DRV8803_SetOutput(const PowerPort_t *port, bool on)
{
    HAL_GPIO_WritePin(port->pins[0].port,
                      port->pins[0].pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief 芯片级使能/禁用
 */
void DRV8803_EnableChip(uint8_t chip_id, bool enable)
{
    if (chip_id == 1) {
        HAL_GPIO_WritePin(DRV1_EN_PORT, DRV1_EN_PIN, enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
    } else if (chip_id == 2) {
        HAL_GPIO_WritePin(DRV2_EN_PORT, DRV2_EN_PIN, enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}

/**
 * @brief 读取芯片故障状态
 */
bool DRV8803_IsChipFault(uint8_t chip_id)
{
    if (chip_id == 1) {
        return (HAL_GPIO_ReadPin(DRV1_FAULT_PORT, DRV1_FAULT_PIN) == GPIO_PIN_RESET);
    } else if (chip_id == 2) {
        return (HAL_GPIO_ReadPin(DRV2_FAULT_PORT, DRV2_FAULT_PIN) == GPIO_PIN_RESET);
    }
    return false;
}

/**
 * @brief 触发芯片硬件复位
 */
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
    HAL_GPIO_WritePin(resetPort, resetPin, GPIO_PIN_SET);   // 进入复位
    HAL_Delay(1);   // 1ms，远大于 20us
    HAL_GPIO_WritePin(resetPort, resetPin, GPIO_PIN_RESET); // 退出复位
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
