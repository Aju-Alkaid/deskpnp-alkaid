#include "driver_heater.h"
#include "driver_can.h"
#include "stm32g4xx_hal.h"
#include "cmsis_os.h"
#include "app_test.h"  /* PrintDebug */

/* ========== 内部变量 ========== */
static HeaterStatus_t g_heater_status = {0};

/* 外部队列句柄（CAN ISR 写入，任务读取） */
osMessageQueueId_t heater_rx_queue = NULL;

/*
 * Heater_Transmit — 加热台原生 CAN 发送（不使用 CAN_Transmit_Data 的自动 CRC）
 *
 * 加热台协议与 MKS 电机协议 CRC 规则不同：
 *   - CRC = (数据字节 0..N 累加和) & 0xFF，不含 CAN ID
 *   - 无参数命令（START/STOP/QUERY）不附加 CRC
 *   - 有参数命令（SET_TEMP/SET_PID）在末尾附加 CRC
 *
 * @param data      数据缓冲区（不含 CRC，由本函数计算追加）
 * @param len       数据长度（不含 CRC 的原始字节数）
 * @param need_crc  true = 附加 CRC，false = 原样发送
 * @return          HAL 状态码（调用方可忽略，预留用于未来错误处理）
 */
static HAL_StatusTypeDef Heater_Transmit(uint8_t *data, uint8_t len, bool need_crc)
{
    FDCAN_TxHeaderTypeDef tx_header = {0};
    uint8_t buf[8] = {0};
    uint8_t total_len = len;

    memcpy(buf, data, len);

    if (need_crc) {
        uint8_t crc = 0;
        for (uint8_t i = 0; i < len; i++) {
            crc += data[i];
        }
        buf[len] = crc;
        total_len = len + 1;
    }

    tx_header.Identifier = HEATER_CMD_ID;
    tx_header.IdType = FDCAN_STANDARD_ID;
    tx_header.TxFrameType = FDCAN_DATA_FRAME;
    tx_header.DataLength = total_len;
    tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_header.BitRateSwitch = FDCAN_BRS_OFF;
    tx_header.FDFormat = FDCAN_CLASSIC_CAN;
    tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_header.MessageMarker = 0;

    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx_header, buf);
}

/* ========== 对外接口 ========== */

void Heater_SendStart(void)
{
    uint8_t cmd = HEATER_CMD_START;
    HAL_StatusTypeDef ret = Heater_Transmit(&cmd, 1, false);
    if (ret == HAL_OK) {
        PrintDebug("[HEATER] START sent OK\r\n");
    } else {
        PrintDebug("[HEATER] START TX FAILED (err=%d)\r\n", (int)ret);
    }
}

void Heater_SendStop(void)
{
    uint8_t cmd = HEATER_CMD_STOP;
    HAL_StatusTypeDef ret = Heater_Transmit(&cmd, 1, false);
    if (ret == HAL_OK) {
        PrintDebug("[HEATER] STOP sent OK\r\n");
    } else {
        PrintDebug("[HEATER] STOP TX FAILED (err=%d)\r\n", (int)ret);
    }
}

void Heater_SendQuery(void)
{
    uint8_t cmd = HEATER_CMD_QUERY;
    HAL_StatusTypeDef ret = Heater_Transmit(&cmd, 1, false);
    if (ret == HAL_OK) {
        PrintDebug("[HEATER] QUERY sent OK\r\n");
    } else {
        PrintDebug("[HEATER] QUERY TX FAILED (err=%d)\r\n", (int)ret);
    }
}

void Heater_SetTemperature(int16_t temp_0_1c)
{
    uint8_t data[3];
    data[0] = HEATER_CMD_SET_TEMP;
    data[1] = (temp_0_1c >> 8) & 0xFF;  /* 大端：高字节在前 */
    data[2] = temp_0_1c & 0xFF;         /* 低字节在后 */
    HAL_StatusTypeDef ret = Heater_Transmit(data, 3, true);     /* 附加 CRC */
    if (ret != HAL_OK) {
        PrintDebug("[HEATER] SET_TEMP TX FAILED (err=%d)\r\n", (int)ret);
    }
}

void Heater_SetPID(int16_t Kp, int16_t Ki, int16_t Kd)
{
    uint8_t data[7];
    data[0] = HEATER_CMD_SET_PID;
    data[1] = (Kp >> 8) & 0xFF;
    data[2] = Kp & 0xFF;
    data[3] = (Ki >> 8) & 0xFF;
    data[4] = Ki & 0xFF;
    data[5] = (Kd >> 8) & 0xFF;
    data[6] = Kd & 0xFF;
    Heater_Transmit(data, 7, true);     /* 附加 CRC */
}

/*
 * print_temp — 安全打印有符号 0.1°C 温度值
 *
 * 避免 C 语言负数除/取模的歧义：先取绝对值，手动输出符号。
 */
static void print_temp(const char *label, int16_t temp_01c)
{
    if (temp_01c < 0) {
        int16_t abs_val = -temp_01c;
        PrintDebug("%s-%d.%d", label, abs_val / 10, abs_val % 10);
    } else {
        PrintDebug("%s%d.%d", label, temp_01c / 10, temp_01c % 10);
    }
}

/*
 * Heater_ProcessStatus — 任务上下文中调用，消费 heater_rx_queue 中的状态帧
 *
 * 加热台状态帧格式（6 字节）:
 *   [0] state       状态码
 *   [1] cur_temp_H  当前温度高字节（有符号 16 位大端，0.1°C）
 *   [2] cur_temp_L  当前温度低字节
 *   [3] tar_temp_H  目标温度高字节
 *   [4] tar_temp_L  目标温度低字节
 *   [5] error       错误码
 */
void Heater_ProcessStatus(void)
{
    if (heater_rx_queue == NULL) {
        return;  /* Heater_Init 尚未调用 */
    }

    CAN_Rx_Packet_t pkt;
    while (osMessageQueueGet(heater_rx_queue, &pkt, NULL, 0) == osOK) {
        /* 仅处理加热台状态帧（按 CAN ID 过滤） */
        if (pkt.ID != HEATER_STATUS_ID) {
            continue;
        }

        if (pkt.DataLength >= 6) {
            g_heater_status.state    = pkt.Data[0];
            g_heater_status.cur_temp = (int16_t)((pkt.Data[1] << 8) | pkt.Data[2]);
            g_heater_status.tar_temp = (int16_t)((pkt.Data[3] << 8) | pkt.Data[4]);
            g_heater_status.error    = pkt.Data[5];
            g_heater_status.timestamp = HAL_GetTick();

            PrintDebug("[HEATER] state=%d ", g_heater_status.state);
            print_temp("cur=", g_heater_status.cur_temp);
            print_temp(" tar=", g_heater_status.tar_temp);
            PrintDebug(" err=%d\r\n", g_heater_status.error);
        } else {
            PrintDebug("[HEATER] bad status frame, len=%d\r\n", pkt.DataLength);
        }
    }
}

/* Heater_GetCurrentStatus — 获取最新状态快照（值拷贝） */
HeaterStatus_t Heater_GetCurrentStatus(void)
{
    return g_heater_status;
}

/* Heater_Init — 初始化加热台驱动（创建接收队列） */
void Heater_Init(void)
{
    if (heater_rx_queue == NULL) {
        heater_rx_queue = osMessageQueueNew(10, sizeof(CAN_Rx_Packet_t), NULL);
        PrintDebug("[HEATER] queue created\r\n");
    }
}
