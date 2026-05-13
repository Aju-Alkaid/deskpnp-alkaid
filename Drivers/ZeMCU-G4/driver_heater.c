#include "driver_heater.h"
#include "driver_can.h"
#include "stm32g4xx_hal.h"
#include "cmsis_os.h"
#include "app_test.h"  // for PrintDebug

/* 内部变量 */
static HeaterStatus_t g_heater_status = {0};
static osMessageQueueId_t heater_rx_queue_internal = NULL;

/* 外部队列句柄 */
osMessageQueueId_t heater_rx_queue = NULL;

/* 发送通用命令 (无参数) */
static void Heater_SendCmd(uint8_t cmd) {
    uint8_t tx[1] = {cmd};
    CAN_Transmit_Data(&hfdcan1, HEATER_CMD_ID, tx, 1);
}

/* 发送带参数命令 (2字节参数) */
static void Heater_SendInt16Cmd(uint8_t cmd, int16_t value) {
    uint8_t tx[3];
    tx[0] = cmd;
    tx[1] = (value >> 8) & 0xFF;
    tx[2] = value & 0xFF;
    CAN_Transmit_Data(&hfdcan1, HEATER_CMD_ID, tx, 3);
}

/* 发送带三个int16参数的命令 (PID) */
static void Heater_SendPIDCmd(uint8_t cmd, int16_t p, int16_t i, int16_t d) {
    uint8_t tx[7];
    tx[0] = cmd;
    tx[1] = (p >> 8) & 0xFF;
    tx[2] = p & 0xFF;
    tx[3] = (i >> 8) & 0xFF;
    tx[4] = i & 0xFF;
    tx[5] = (d >> 8) & 0xFF;
    tx[6] = d & 0xFF;
    CAN_Transmit_Data(&hfdcan1, HEATER_CMD_ID, tx, 7);
}

/* 对外接口 */
void Heater_SendStart(void) {
    Heater_SendCmd(HEATER_CMD_START);
}

void Heater_SendStop(void) {
    Heater_SendCmd(HEATER_CMD_STOP);
}

void Heater_SetTemperature(int16_t temp_0_1c) {
    Heater_SendInt16Cmd(HEATER_CMD_SET_TEMP, temp_0_1c);
}

void Heater_SetPID(int16_t Kp, int16_t Ki, int16_t Kd) {
    Heater_SendPIDCmd(HEATER_CMD_SET_PID, Kp, Ki, Kd);
}

/* 在 task 中调用此函数处理接收到的状态数据包，更新全局状态并打印 */
void Heater_ProcessStatus(void) {
    CAN_Rx_Packet_t pkt;
    while (osMessageQueueGet(heater_rx_queue, &pkt, NULL, 0) == osOK) {
        if (pkt.ID != HEATER_STATUS_ID || pkt.FuncCode != HEATER_STATUS_ID) {
            // 不是加热台状态包，丢弃
            continue;
        }
        // 解析数据 (直接使用Data域，因为我们的数据是自有协议，无功能码在Data[0])
        if (pkt.DataLength >= 6) {
            g_heater_status.state    = pkt.Data[0];
            g_heater_status.cur_temp = (int16_t)((pkt.Data[1] << 8) | pkt.Data[2]);
            g_heater_status.tar_temp = (int16_t)((pkt.Data[3] << 8) | pkt.Data[4]);
            g_heater_status.error    = pkt.Data[5];
            g_heater_status.timestamp = HAL_GetTick();

            PrintDebug("Heater: st=%d, cur=%d.%d, tar=%d.%d, err=%d\r\n",
                       g_heater_status.state,
                       g_heater_status.cur_temp/10, g_heater_status.cur_temp%10,
                       g_heater_status.tar_temp/10, g_heater_status.tar_temp%10,
                       g_heater_status.error);
        } else {
            PrintDebug("Heater: bad status len=%d\r\n", pkt.DataLength);
        }
    }
}

/* 获取最新状态 (线程安全，直接返回拷贝) */
HeaterStatus_t Heater_GetCurrentStatus(void) {
    return g_heater_status;
}

/* 初始化：创建队列，注册过滤器（如果需要） */
void Heater_Init(void) {
    if (heater_rx_queue == NULL) {
        heater_rx_queue = osMessageQueueNew(10, sizeof(CAN_Rx_Packet_t), NULL);
        heater_rx_queue_internal = heater_rx_queue;
        PrintDebug("Heater queue created\r\n");
    }
}