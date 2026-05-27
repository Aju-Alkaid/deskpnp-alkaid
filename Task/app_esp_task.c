/**
 * @file    app_esp_task.c
 * @brief   ESP32 通信任务实现
 * @note    周期 500ms 心跳推送数据，轮询分时发送 (每次只发一个字段)
 *          控制命令通过 esp_cmd_queue 优先处理
 *          版本: v2
 */

#include "app_esp_task.h"
#include "app_esp_protocol.h"
#include "driver_esp32.h"
#include "app_test.h"                            /* PrintDebug */
#include "gui/model/Data_Transfer.h"             /* now_SMT, total_SMT, Temp, if_now_SMT, if_DOWNLOAD_READY */
#include "driver_heater.h"                       /* Heater_GetCurrentStatus */
#include <string.h>

/* ================================================================
 *  全局状态变量
 * ================================================================ */
uint8_t  g_esp_wifi_enabled   = 0;   /* 上电默认关闭，收到 0x20 0x01 后开启 */
uint8_t  g_esp_wifi_connected = 0;
uint8_t  g_esp_fault_code     = 0x00;
uint32_t g_esp_last_rx_tick   = 0;

/* ================================================================
 *  队列句柄
 * ================================================================ */
/* esp_cmd_queue ??? app_freertos.c????? app_esp_task.h extern ?? */

/* ================================================================
 *  任务内静态变量 (不占栈)
 * ================================================================ */
static uint8_t s_tx_buf[128];        /* 发送缓冲区 (.bss) */
static uint8_t s_rx_buf[128];        /* 接收缓冲区 (.bss) */
static uint8_t s_round_robin_index;  /* 轮询分时索引 0~3 */
static uint8_t s_no_resp_count;      /* 连续无响应计数 */
static uint16_t s_last_temp;         /* 上一次发送的温度值 (0.1°C)，用于变化检测 */

/* ---- 数据字段轮询表 ---- */
/* 顺序: 进度 -> 状态 -> 加热台状态 -> 加热台温度 */
#define RR_PROGRESS       0
#define RR_SMT_STATUS     1
#define RR_HEATER_STATE   2
#define RR_HEATER_TEMP    3
#define RR_COUNT          4

/* ================================================================
 *  内部函数声明
 * ================================================================ */

static void _send_data_field(uint8_t field_id);
static void _process_control_cmd(ESP_Cmd_t cmd);
static void _process_response(void);
static void _handle_fault_response(const char *payload, uint8_t len);
static void _handle_wifi_response(const char *payload, uint8_t len);
static void _handle_compound_response(const char *payload, uint8_t len);

/* ================================================================
 *  ESP_SendCommand — 便捷接口
 * ================================================================ */
void ESP_SendCommand(ESP_Cmd_t cmd)
{
    if (esp_cmd_queue != NULL) {
        osMessageQueuePut(esp_cmd_queue, &cmd, 0, 0);
    }
}

/* ================================================================
 *  ESP_Task — 任务主循环
 * ================================================================ */
void ESP_Task(void *argument)
{
    ESP_Cmd_t cmd;

    /* ---- 初始化阶段 ---- */
    ESP_GPIO_Init();                  /* CS=HIGH, 释放复位 */
    ESP_HardReset();                  /* 硬件复位 ESP32 */
    osDelay(1000);                    /* 等待 ESP32 启动完成 */

    PrintDebug("[ESP] Task started, waiting for WiFi open command\r\n");

    /* ---- 主循环 ---- */
    for (;;) {
        /*
         * 阻塞等待: 500ms 心跳超时 或 esp_cmd_queue 有新命令
         * 使用 osThreadFlagsWait 实现事件驱动 + 周期心跳
         */
        uint32_t flags = osThreadFlagsWait(0x01, osFlagsWaitAny,
                                           pdMS_TO_TICKS(500));

        /*
         * 优先处理控制命令队列 (不阻塞，一次处理所有积压命令)
         */
        while (osMessageQueueGet(esp_cmd_queue, &cmd, NULL, 0) == osOK) {
            _process_control_cmd(cmd);
            /* 控制命令后立即读一次 ESP 响应 */
            _process_response();
        }

        /*
         * 心跳超时 → 检查是否 WiFi 已开启
         * WiFi 未开启时只发心跳包 (被动等待响应)
         * WiFi 开启后按轮询表发送数据字段
         */
        if (!g_esp_wifi_enabled) {
            /* WiFi 未开启: 发心跳包，仅读取 ESP 响应 (可能含版本/状态) */
            ESP_BuildHeartbeatPacket(s_tx_buf);
            ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
            _process_response();
        } else {
            /* WiFi 已开启: 轮询分时发送数据字段 */
            _send_data_field(s_round_robin_index);
            s_round_robin_index++;
            if (s_round_robin_index >= RR_COUNT) {
                s_round_robin_index = 0;
            }

            /* SPI 全双工收发 */
            ESP_SPI_Transfer(s_tx_buf, s_rx_buf);

            /* 处理 ESP 响应 */
            _process_response();
        }
    }
}

/* ================================================================
 *  轮询分时发送数据字段
 * ================================================================ */
static void _send_data_field(uint8_t field_id)
{
    char payload[16];
    int  len;

    switch (field_id) {

    case RR_PROGRESS:
        len = ESP_FormatProgress(payload, sizeof(payload),
                                 now_SMT, total_SMT);
        ESP_BuildDataPacket(s_tx_buf, ESP_SUB_PROGRESS,
                            payload, (uint8_t)len);
        break;

    case RR_SMT_STATUS:
        {
            /* 从现有状态变量推导贴片状态 */
            HeaterStatus_t hs = Heater_GetCurrentStatus();
            uint8_t is_heating = (hs.state > 0 && hs.state < 5) ? 1 : 0;
            const char *state_str = ESP_StateToString(
                if_now_SMT, if_DOWNLOAD_READY, is_heating, 0);
            len = (int)strlen(state_str);
            if (len > 15) len = 15;
            memcpy(payload, state_str, len);
            ESP_BuildDataPacket(s_tx_buf, ESP_SUB_SMT_STATUS,
                                payload, (uint8_t)len);
        }
        break;

    case RR_HEATER_STATE:
        {
            HeaterStatus_t hs = Heater_GetCurrentStatus();
            /* 加热中: PREHEAT(1), SOAK(2), REFLOW(3), COOLING(4)
               非加热: STANDBY(0), COMPLETE(5), ERROR(6) */
            uint8_t is_on = (hs.state > 0 && hs.state < 5) ? 1 : 0;
            payload[0] = is_on ? '1' : '0';
            ESP_BuildDataPacket(s_tx_buf, ESP_SUB_HEATER_STATE,
                                payload, 1);
        }
        break;

    case RR_HEATER_TEMP:
        {
            HeaterStatus_t hs = Heater_GetCurrentStatus();
            uint16_t cur_temp = (uint16_t)hs.cur_temp;

            /* 温度变化 < 0.5°C 时跳过本次发送 (发心跳包代替) */
            int16_t diff = (int16_t)(cur_temp - s_last_temp);
            if (diff < 0) diff = -diff;
            if (diff < 5 && s_last_temp != 0) {
                ESP_BuildHeartbeatPacket(s_tx_buf);
                break;
            }
            s_last_temp = cur_temp;

            len = ESP_FormatTemp(payload, sizeof(payload), cur_temp);
            ESP_BuildDataPacket(s_tx_buf, ESP_SUB_HEATER_TEMP,
                                payload, (uint8_t)len);
        }
        break;

    default:
        ESP_BuildHeartbeatPacket(s_tx_buf);
        break;
    }
}

/* ================================================================
 *  控制命令处理
 * ================================================================ */
static void _process_control_cmd(ESP_Cmd_t cmd)
{
    switch (cmd) {

    case ESP_CMD_WIFI_ON:
        PrintDebug("[ESP] WiFi ON command\r\n");
        ESP_BuildControlPacket(s_tx_buf, ESP_SUB_WIFI_ON);
        ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
        g_esp_wifi_enabled = 1;
        break;

    case ESP_CMD_WIFI_OFF:
        PrintDebug("[ESP] WiFi OFF command\r\n");
        ESP_BuildControlPacket(s_tx_buf, ESP_SUB_WIFI_OFF);
        ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
        g_esp_wifi_enabled  = 0;
        g_esp_wifi_connected = 0;
        break;

    case ESP_CMD_QUERY_FAULT:
        ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_FAULT);
        ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
        break;

    case ESP_CMD_QUERY_WIFI:
        ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_WIFI);
        ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
        break;

    default:
        break;
    }
}

/* ================================================================
 *  ESP 响应处理
 * ================================================================ */
static void _process_response(void)
{
    uint8_t resp_type = ESP_GetResponseType(s_rx_buf);

    if (resp_type == ESP_RESP_IDLE) {
        /* 空闲: 无待报告信息 */
        s_no_resp_count = 0;  /* 收到 0x00 也算有效响应 (非超时) */
        return;
    }

    /* 有效响应: 更新最后接收 tick，清零无响应计数 */
    g_esp_last_rx_tick = osKernelGetTickCount();
    s_no_resp_count = 0;

    uint8_t payload_len;
    const char *payload = ESP_GetResponsePayload(s_rx_buf, &payload_len);

    switch (resp_type) {

    case ESP_RESP_FAULT:
        _handle_fault_response(payload, payload_len);
        break;

    case ESP_RESP_WIFI_STATUS:
        _handle_wifi_response(payload, payload_len);
        break;

    case ESP_RESP_COMPOUND:
        _handle_compound_response(payload, payload_len);
        break;

    case ESP_RESP_VERSION:
        PrintDebug("[ESP] Version: %.*s\r\n", payload_len, payload);
        break;

    default:
        /* 未知响应类型: 静默忽略 */
        break;
    }
}

/* ---- 响应处理子函数 ---- */

static void _handle_fault_response(const char *payload, uint8_t len)
{
    /* 简单解析: 取第一个逗号前的数字作为故障码 */
    if (len > 0 && payload[0] >= '0' && payload[0] <= '9') {
        g_esp_fault_code = (uint8_t)(payload[0] - '0');
    }
    PrintDebug("[ESP] Fault: %.*s (code=%d)\r\n",
               len, payload, g_esp_fault_code);
}

static void _handle_wifi_response(const char *payload, uint8_t len)
{
    if (len > 0) {
        g_esp_wifi_connected = (payload[0] == '1') ? 1 : 0;
    }
    PrintDebug("[ESP] WiFi status: %s\r\n",
               g_esp_wifi_connected ? "connected" : "disconnected");
}

static void _handle_compound_response(const char *payload, uint8_t len)
{
    /* 复合格式: "F1:code|F2:1" — 按 '|' 分割后逐个解析 */
    PrintDebug("[ESP] Compound: %.*s\r\n", len, payload);

    /* 遍历查找 "F1:" 和 "F2:" 前缀 */
    for (uint8_t i = 0; i < len; ) {
        /* 查找 '|' 或结尾 */
        uint8_t seg_start = i;
        while (i < len && payload[i] != '|') i++;

        uint8_t seg_len = i - seg_start;
        if (seg_len >= 4 && payload[seg_start] == 'F' && payload[seg_start+1] == '1') {
            if (payload[seg_start+3] >= '0' && payload[seg_start+3] <= '9') {
                g_esp_fault_code = (uint8_t)(payload[seg_start+3] - '0');
            }
        } else if (seg_len >= 4 && payload[seg_start] == 'F' && payload[seg_start+1] == '2') {
            g_esp_wifi_connected = (payload[seg_start+3] == '1') ? 1 : 0;
        }

        /* 跳过 '|' */
        if (i < len && payload[i] == '|') i++;
    }
}
