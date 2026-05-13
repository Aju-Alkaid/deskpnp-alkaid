#include "driver_tmc2209.h"
#include "main.h"
#include <string.h>
#include <math.h>
#include "FreeRTOS.h"
#include "semphr.h"

extern void PrintDebug(const char* fmt, ...);

/* ========== 互斥锁 ========== */
static SemaphoreHandle_t g_tmc_mutex = NULL;

/* ========== CRC-8 (ATM) ========== */
static uint8_t TMC_CalcCRC(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if ((crc >> 7) ^ (byte & 0x01))
                crc = (crc << 1) ^ 0x07;
            else
                crc = (crc << 1);
            byte >>= 1;
        }
    }
    return crc;
}

/* ========== 清空 UART3 接收缓冲区（丢弃残留数据） ========== */
static void TMC_FlushRX(void) {
    while (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE)) {
        volatile uint32_t tmp = huart3.Instance->RDR;
        (void)tmp;
    }
}

/* ========== 底层 UART 写寄存器（全双工阻塞） ========== */
static TMC_Error_t TMC_WriteReg_Internal(uint8_t reg, uint32_t data) {
    uint8_t packet[TMC_DATAGRAM_LEN];
    uint8_t regAddr = reg | TMC_WRITE_FLAG;

    packet[0] = TMC_SYNC_BYTE;
    packet[1] = TMC_SLAVE_ADDR;
    packet[2] = regAddr;
    packet[3] = (data >> 24) & 0xFF;
    packet[4] = (data >> 16) & 0xFF;
    packet[5] = (data >> 8) & 0xFF;
    packet[6] = (data >> 0) & 0xFF;
    packet[7] = TMC_CalcCRC(packet, TMC_DATAGRAM_LEN - 1);

    if (HAL_UART_Transmit(&huart3, packet, TMC_DATAGRAM_LEN, 10) != HAL_OK) {
        return TMC_ERR_BUSY;
    }

    /* 等待发送完成 */
    while (!__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC));
    /* 清空可能被自身 TX 干扰收到的数据 */
    TMC_FlushRX();

    /* TMC2209 数据手册要求写操作后需要 ≥NCLK 的延时才能进行下一次操作，这里用 1ms 保证 */
    HAL_Delay(1);
    return TMC_ERR_NONE;
}

/* ========== 底层 UART 读寄存器（全双工阻塞） ========== */
static TMC_Error_t TMC_ReadReg_Internal(uint8_t reg, uint32_t *data) {
    uint8_t req[TMC_READ_REQUEST_LEN];
    uint8_t reply[TMC_DATAGRAM_LEN];
    uint8_t crc_calc;

    if (data == NULL) return TMC_ERR_INVALID_PARAM;

    /* 构造读请求 */
    req[0] = TMC_SYNC_BYTE;
    req[1] = TMC_SLAVE_ADDR;
    req[2] = reg & 0x7F;
    req[3] = TMC_CalcCRC(req, TMC_READ_REQUEST_LEN - 1);

    /* 发送前清空接收区，并停止可能的残留接收 */
    HAL_UART_AbortReceive(&huart3);
    TMC_FlushRX();

    /* 发送读请求 */
    if (HAL_UART_Transmit(&huart3, req, TMC_READ_REQUEST_LEN, 10) != HAL_OK) {
        return TMC_ERR_BUSY;
    }

    /* 等待发送完成，清回声 */
    while (!__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC));
    TMC_FlushRX();

    /* 接收 8 字节应答，超时设为 20ms */
    if (HAL_UART_Receive(&huart3, reply, TMC_DATAGRAM_LEN, 20) != HAL_OK) {
        return TMC_ERR_TIMEOUT;
    }

    /* 校验头 */
    if (reply[0] != TMC_SYNC_BYTE || reply[1] != TMC_READ_REPLY_ADDR) {
        return TMC_ERR_SYNC_FAIL;
    }

    /* 校验 CRC */
    crc_calc = TMC_CalcCRC(reply, TMC_DATAGRAM_LEN - 1);
    if (crc_calc != reply[TMC_DATAGRAM_LEN - 1]) {
        return TMC_ERR_CRC_FAIL;
    }

    *data = ((uint32_t)reply[3] << 24) |
            ((uint32_t)reply[4] << 16) |
            ((uint32_t)reply[5] << 8)  |
            ((uint32_t)reply[6]);

    return TMC_ERR_NONE;
}

/* ========== 带锁的公开 API ========== */
TMC_Error_t TMC_WriteReg(uint8_t reg_addr, uint32_t data) {
    if (g_tmc_mutex == NULL) return TMC_ERR_INVALID_PARAM;
    if (xSemaphoreTake(g_tmc_mutex, pdMS_TO_TICKS(TMC_MUTEX_WAIT_TIME)) != pdTRUE)
        return TMC_ERR_BUSY;

    TMC_Error_t ret = TMC_WriteReg_Internal(reg_addr, data);
    xSemaphoreGive(g_tmc_mutex);
    return ret;
}

TMC_Error_t TMC_ReadReg(uint8_t reg_addr, uint32_t *value) {
    if (g_tmc_mutex == NULL || value == NULL) return TMC_ERR_INVALID_PARAM;
    if (xSemaphoreTake(g_tmc_mutex, pdMS_TO_TICKS(TMC_MUTEX_WAIT_TIME)) != pdTRUE)
        return TMC_ERR_BUSY;

    TMC_Error_t ret = TMC_ReadReg_Internal(reg_addr, value);
    xSemaphoreGive(g_tmc_mutex);
    return ret;
}

/* ---------- 高级设置 ---------- */
void TMC_SetEnable(bool enable) {
    GPIO_PinState state = enable ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(TMC_EN_PORT, TMC1_EN_PIN, state);
}

void TMC_SetCurrent_Raw(uint8_t run_current, uint8_t hold_current, uint8_t hold_delay) {
    if (run_current > 31) run_current = 31;
    if (hold_current > 31) hold_current = 31;
    if (hold_delay > 15) hold_delay = 15;
    uint32_t val = (hold_delay << 16) | (run_current << 8) | hold_current;
    TMC_WriteReg(TMC_REG_IHOLD_IRUN, val);
}

void TMC_SetCurrent_mA(uint16_t current_mA) {
    float V_fs = (TMC2209_VSENSE == 1) ? 0.180f : 0.325f;
    float R_sense = (float)TMC2209_RSENSE_MOHM / 1000.0f + 0.02f;
    float i_max = V_fs / R_sense * 0.707f;
    float desired_A = current_mA / 1000.0f;

    int cs = (int)(desired_A / i_max * 32.0f) - 1;
    if (cs < 0) cs = 0;
    if (cs > 31) cs = 31;

    int ihold = cs / 2;
    if (ihold < 8) ihold = 8;
    if (ihold > 31) ihold = 31;

    TMC_SetCurrent_Raw(cs, ihold, 2);
}

void TMC_SetMicrosteps(uint16_t steps) {
    uint32_t chopconf;
    if (TMC_ReadReg(TMC_REG_CHOPCONF, &chopconf) != TMC_ERR_NONE) return;

    chopconf &= ~(0x0F << 24);
    uint8_t mres;
    switch (steps) {
        case 256: mres = 0; break;
        case 128: mres = 1; break;
        case 64:  mres = 2; break;
        case 32:  mres = 3; break;
        case 16:  mres = 4; break;
        case 8:   mres = 5; break;
        case 4:   mres = 6; break;
        case 2:   mres = 7; break;
        case 1:   mres = 8; break;
        default:  return;
    }
    chopconf |= (mres << 24);
    if ((chopconf & 0x0F) == 0) chopconf |= 3;
    TMC_WriteReg(TMC_REG_CHOPCONF, chopconf);
}

void TMC_SetChopperMode(bool use_spreadcycle) {
    uint32_t gconf;
    if (TMC_ReadReg(TMC_REG_GCONF, &gconf) != TMC_ERR_NONE) return;
    if (use_spreadcycle)
        gconf |= GCONF_EN_SPREADCYCLE;
    else
        gconf &= ~GCONF_EN_SPREADCYCLE;
    TMC_WriteReg(TMC_REG_GCONF, gconf);
}

void TMC_SetSpeed(int32_t velocity) {
    float v_freq = (float)velocity;
    int32_t vactual = (int32_t)(v_freq * 16777216.0f / (TMC2209_F_CLK / 2.0f));
    if (vactual > 8388607) vactual = 8388607;
    if (vactual < -8388607) vactual = -8388607;
    TMC_WriteReg(TMC_REG_VACTUAL, (uint32_t)vactual);
}

TMC_Error_t TMC_GetStatus(uint8_t *status) {
    uint32_t gstat;
    TMC_Error_t ret = TMC_ReadReg(TMC_REG_GSTAT, &gstat);
    if (ret == TMC_ERR_NONE) *status = (uint8_t)(gstat & 0xFF);
    return ret;
}

TMC_Error_t TMC_GetSGResult(uint16_t *sg_value) {
    uint32_t val;
    TMC_Error_t ret = TMC_ReadReg(TMC_REG_SG_RESULT, &val);
    if (ret == TMC_ERR_NONE) *sg_value = (uint16_t)(val & 0x3FF);
    return ret;
}

/* ========== 初始化 ========== */
bool TMC_Init(void) {
    if (g_tmc_mutex == NULL) {
        g_tmc_mutex = xSemaphoreCreateMutex();
        if (g_tmc_mutex == NULL) return false;
    }

    /* 配置 EN 引脚 */
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOD_CLK_ENABLE();
    gpio.Pin = TMC1_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TMC_EN_PORT, &gpio);

    TMC_SetEnable(false);
    HAL_Delay(10);

    /* 关键：接管 UART3，停止 DMA 接收（因全双工阻塞模式不需要） */
    HAL_UART_DMAStop(&huart3);          // 停止 DMA
    HAL_UART_Abort(&huart3);            // 终止任何进行中的接收
    __HAL_UART_DISABLE_IT(&huart3, UART_IT_IDLE); // 禁用空闲中断，防止干扰
    TMC_FlushRX();

    /* 1. GCONF */
    uint32_t gconf = GCONF_PDN_DISABLE | GCONF_MSTEP_REG_SELECT | GCONF_I_SCALE_ANALOG;
    TMC_WriteReg(TMC_REG_GCONF, gconf);
    vTaskDelay(pdMS_TO_TICKS(1));

    /* 2. CHOPCONF */
    uint32_t chopconf = CHOPCONF_INTPOL
                      | (TMC2209_VSENSE ? CHOPCONF_VSENSE : 0)
                      | (2 << CHOPCONF_TBL_SHIFT)
                      | (0 << CHOPCONF_HEND_SHIFT)
                      | (5 << CHOPCONF_HSTRT_SHIFT)
                      | (3 << CHOPCONF_TOFF_SHIFT);
    chopconf |= 0x10000000;  // MRES=256
    TMC_WriteReg(TMC_REG_CHOPCONF, chopconf);
    vTaskDelay(pdMS_TO_TICKS(1));

    /* 3. PWMCONF */
    uint32_t pwmconf = (1 << 18) | (1 << 19) | (2 << 16) | (4 << 28);
    TMC_WriteReg(TMC_REG_PWMCONF, pwmconf);
    vTaskDelay(pdMS_TO_TICKS(1));

    /* 4. 电流 */
    TMC_SetCurrent_mA(TMC2209_MOTOR_RUN_CURRENT);
    vTaskDelay(pdMS_TO_TICKS(1));

    /* 5. TPOWERDOWN */
    TMC_WriteReg(TMC_REG_TPOWERDOWN, 20);
    vTaskDelay(pdMS_TO_TICKS(1));

    /* 6. 使能 */
    TMC_SetEnable(true);
    vTaskDelay(pdMS_TO_TICKS(500));

    PrintDebug("TMC2209 Full-Duplex Initialized\r\n");
    return true;
}