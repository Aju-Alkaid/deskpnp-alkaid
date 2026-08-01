/**
 * @file    app_esp_task.c
 * @brief   ESP32 通信任务实现
 * @note    版本: v2 (新增 IRQ 场景B, CMD 0x40/0x50/0x60, 复合响应新格式)
 *
 *          【场景 A — STM32 主动下发】
 *          STM32 检查 IRQ 高 → 发送 128 字节 (MOSI) + 接收 128 字节 (MISO)
 *          → 延时 2ms → 再次检查 IRQ
 *
 *          【场景 B — ESP32 主动上报 (v2 新增)】
 *          IRQ 下降沿中断 → ISR 置 esp32_irq_flag=1
 *          → 主循环检测 → 发送哑元帧读取 ESP 数据 → 延时 2ms
 *          → 分发 CMD 0x40/0x60 → 通知 Host_Task
 *
 *          【互斥规则】
 *          场景 A 的每次 SPI 传输前必须检查 IRQ，IRQ 低则优先场景 B。
 */

#include "app_esp_task.h"
#include "app_esp_protocol.h"
#include "driver_esp32.h"
#include "app_test.h"                            /* PrintDebug */
#include "app_gui_spi.h"             /* now_SMT, total_SMT */
#include "driver_heater.h"                       /* Heater_GetCurrentStatus */
#include <string.h>

/* ================================================================
 *  全局状态变量
 * ================================================================ */
uint8_t  g_esp_wifi_enabled   = 0;
uint8_t  g_esp_wifi_connected = 0;
uint8_t  g_esp_fault_code     = 0x00;
uint32_t g_esp_last_rx_tick   = 0;

/* ★ v2 新增: 网页下发命令标志 */
volatile uint8_t     g_esp_web_cmd_pending = 0;
volatile ESP_Cmd_t   g_esp_web_cmd        = 0;

/* ================================================================
 *  队列句柄 (app_freertos.c 中创建, app_esp_task.h extern 声明)
 * ================================================================ */

/* ================================================================
 *  任务内静态变量 (不占栈)
 * ================================================================ */
static uint8_t s_tx_buf[128];        /* 发送缓冲区 */
static uint8_t s_rx_buf[128];        /* 接收缓冲区 */
static uint8_t s_round_robin_index;  /* 轮询分时索引 0~3 */
static uint8_t s_no_resp_count;      /* 连续无响应计数 */
static uint16_t s_last_temp;         /* 上次发送的温度 (0.1°C) */
static uint32_t s_last_heartbeat_tick;

/* ---- 数据字段轮询表 ---- */
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

/* ★ v2 新增 */
static void _scene_b_read(void);       /* 场景 B: 读取 ESP→STM32 命令 */
static void _check_irq_and_read(void); /* IRQ 互斥检查 + 场景 B 执行  */

/* ================================================================
 *  ESP_SendCommand — 便捷接口 (不变)
 * ================================================================ */
void ESP_SendCommand(ESP_Cmd_t cmd)
{
    if (esp_cmd_queue != NULL) {
        osMessageQueuePut(esp_cmd_queue, &cmd, 0, 0);
    }
}

/* ================================================================
 *  ESP_SendLog — 发送日志到 ESP32 (CMD_LOG_DATA 0x50) ★ v2 新增
 *
 *  Host_Task 在执行 SMT 步骤时调用，每条日志触发一次 SPI 传输。
 *  使用独立的局部缓冲区避免与 ESP_Task 主循环的 s_tx_buf 冲突。
 *  发送前检查 IRQ 互斥（场景 B 优先）。
 * ================================================================ */
void ESP_SendLog(const char *text)
{
    uint8_t tx_buf[128];
    uint8_t rx_buf[128];

    if (text == NULL) return;

    ESP_BuildLogPacket(tx_buf, text);

    /* IRQ 互斥: 先检查是否有 ESP 待发数据 */
    _check_irq_and_read();

    /* SPI 全双工收发 */
    ESP_SPI_Transfer(tx_buf, rx_buf);

    /* 简单处理响应 (不重复解析场景 B 命令) */
    if (rx_buf[0] == 0x00) {
        uint8_t rt = ESP_GetResponseType(rx_buf);
        if (rt != ESP_RESP_IDLE) {
            g_esp_last_rx_tick = osKernelGetTickCount();
        }
    }
}

/* ================================================================
 *  场景 B: 读取 ESP→STM32 主动上报的命令 ★ v2 新增
 *
 *  文档 §5.2: ESP32 拉低 IRQ → STM32 发送哑元帧 (全 0xFF)
 *  → 从 MISO 读取 ESP 数据 → 延时 2ms → ESP 拉高 IRQ
 * ================================================================ */
static void _scene_b_read(void)
{
    uint8_t dummy[128];
    uint8_t rx[128];
    uint8_t cmd, sub;

    /* 发送全 0xFF 哑元帧 */
    memset(dummy, 0xFF, 128);
    ESP_SPI_Transfer(dummy, rx);

    /* 延时 2ms 防抖 (文档 §5.2) */
    osDelay(2);

    /* 解析 ESP→STM32 命令 */
    cmd = ESP_GetCmd(rx);
    if (cmd != ESP_CMD_PROCESS_CTRL && cmd != ESP_CMD_HEATER_CTRL) {
        return; /* 非有效命令，静默忽略 */
    }

    sub = ESP_GetSubCmd(rx);

    if (cmd == ESP_CMD_PROCESS_CTRL) {
        switch (sub) {
            case ESP_SUB_PROC_START:  g_esp_web_cmd = ESP_CMD_PROC_START;  break;
            case ESP_SUB_PROC_PAUSE:  g_esp_web_cmd = ESP_CMD_PROC_PAUSE;  break;
            case ESP_SUB_PROC_RESUME: g_esp_web_cmd = ESP_CMD_PROC_RESUME; break;
            case ESP_SUB_PROC_STOP:   g_esp_web_cmd = ESP_CMD_PROC_STOP;   break;
            case ESP_SUB_PROC_ESTOP:  g_esp_web_cmd = ESP_CMD_PROC_ESTOP;  break;
            default: return;
        }
        g_esp_web_cmd_pending = 1;
        PrintDebug("[ESP] Scene B: Web PROC cmd 0x%02X\r\n", sub);
    }
    else if (cmd == ESP_CMD_HEATER_CTRL) {
        if (sub == ESP_SUB_HEAT_START)      g_esp_web_cmd = ESP_CMD_HEAT_START;
        else if (sub == ESP_SUB_HEAT_STOP)  g_esp_web_cmd = ESP_CMD_HEAT_STOP;
        else return;
        g_esp_web_cmd_pending = 1;
        PrintDebug("[ESP] Scene B: Web HEATER cmd 0x%02X\r\n", sub);
    }
}

/*
 * IRQ 互斥检查: 检查 esp32_irq_flag 和引脚电平
 * 若 IRQ 为低则优先执行场景 B
 */
static void _check_irq_and_read(void)
{
    if (esp32_irq_flag || !ESP_CheckIRQ()) {
        esp32_irq_flag = 0;
        _scene_b_read();
    }
}

/* ================================================================
 *  ESP_Task — 任务主循环 ★ v2 重写
 * ================================================================ */
void ESP_Task(void *argument)
{
    ESP_Cmd_t cmd;

    /* ---- 初始化 ---- */
    ESP_GPIO_Init();                  /* CS=HIGH, RST=HIGH, IRQ EXTI */
    ESP_HardReset();                  /* 硬件复位 ESP32 */
    osDelay(1000);                    /* 等待 ESP32 启动完成 */

    PrintDebug("[ESP] Task started v2 (IRQ enabled, waiting for WiFi open)\r\n");

    /* ---- 主循环 ---- */
    for (;;) {
        /*
         * 步骤1: 优先检查 IRQ 标志/引脚 (场景 B)
         * 在阻塞等待前先处理可能已经到来的网页命令
         */
        _check_irq_and_read();

        /*
         * 步骤2: 阻塞等待 500ms 轮询超时 或 esp_cmd_queue 有新命令
         */
        uint32_t flags = osThreadFlagsWait(0x01, osFlagsWaitAny,
                                           pdMS_TO_TICKS(500));

        /*
         * 步骤3: 优先处理控制命令队列 (不阻塞，一次处理所有积压)
         */
        while (osMessageQueueGet(esp_cmd_queue, &cmd, NULL, 0) == osOK) {
            _process_control_cmd(cmd);
            _process_response();
        }

        /*
         * 步骤4: 轮询超时 → 周期数据发送 (场景 A)
         */
        if (!g_esp_wifi_enabled) {
            /* WiFi 未开启: 每 30s 发一次心跳包 */
            uint32_t now = osKernelGetTickCount();
            if ((now - s_last_heartbeat_tick) >= pdMS_TO_TICKS(30000)) {
                s_last_heartbeat_tick = now;
                ESP_BuildHeartbeatPacket(s_tx_buf);
                _check_irq_and_read();
                ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
                _process_response();
            }
        } else {
            /* WiFi 已开启: 轮询分时发送数据字段 */
            _send_data_field(s_round_robin_index);
            s_round_robin_index++;
            if (s_round_robin_index >= RR_COUNT) {
                s_round_robin_index = 0;
            }

            /*
             * ★ 场景 A 传输前 IRQ 互斥检查
             * IRQ 低 → 优先场景 B → 发哑元帧读 ESP 命令
             * IRQ 高 → 正常发送数据字段
             */
            _check_irq_and_read();

            ESP_SPI_Transfer(s_tx_buf, s_rx_buf);

            _process_response();
        }
    }
}

/* ================================================================
 *  轮询分时发送数据字段 (不变)
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
 *  控制命令处理 ★ v2 新增 ESP_CMD_QUERY_ALL
 * ================================================================ */
static void _process_control_cmd(ESP_Cmd_t cmd)
{
    switch (cmd) {

    case ESP_CMD_WIFI_ON:
        PrintDebug("[ESP] WiFi ON command\r\n");
        ESP_BuildControlPacket(s_tx_buf, ESP_SUB_WIFI_ON);
        _check_irq_and_read();
        ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
        g_esp_wifi_enabled = 1;
        break;

    case ESP_CMD_WIFI_OFF:
        PrintDebug("[ESP] WiFi OFF command\r\n");
        ESP_BuildControlPacket(s_tx_buf, ESP_SUB_WIFI_OFF);
        _check_irq_and_read();
        ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
        g_esp_wifi_enabled  = 0;
        g_esp_wifi_connected = 0;
        break;

    case ESP_CMD_QUERY_FAULT:
        ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_FAULT);
        _check_irq_and_read();
        ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
        break;

    case ESP_CMD_QUERY_WIFI:
        ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_WIFI);
        _check_irq_and_read();
        ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
        break;

    case ESP_CMD_QUERY_ALL:     /* ★ v2 新增 */
        ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_ALL);
        _check_irq_and_read();
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
    /* 防御 MISO 浮空: 有效响应的 Byte 0 固定为 0x00 */
    if (s_rx_buf[0] != 0x00) {
        return;
    }

    uint8_t resp_type = ESP_GetResponseType(s_rx_buf);

    if (resp_type == ESP_RESP_IDLE) {
        s_no_resp_count = 0;
        return;
    }

    /* 响应类型白名单检查 */
    if (resp_type != ESP_RESP_FAULT &&
        resp_type != ESP_RESP_WIFI_STATUS &&
        resp_type != ESP_RESP_COMPOSITE &&
        resp_type != ESP_RESP_VERSION) {
        return;
    }

    /* payload 长度合法性检查 */
    if (s_rx_buf[2] > 123) {
        return;
    }

    /* 有效响应 */
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

    case ESP_RESP_COMPOSITE:
        _handle_compound_response(payload, payload_len);
        break;

    case ESP_RESP_VERSION:
        PrintDebug("[ESP] Version: %.*s\r\n", payload_len, payload);
        break;

    default:
        break;
    }
}

/* ---- 响应处理子函数 ---- */

static void _handle_fault_response(const char *payload, uint8_t len)
{
    uint8_t code = 0;
    for (uint8_t i = 0; i < len && i < 2; i++) {
        char c = payload[i];
        code <<= 4;
        if (c >= '0' && c <= '9')      code |= (uint8_t)(c - '0');
        else if (c >= 'A' && c <= 'F') code |= (uint8_t)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') code |= (uint8_t)(c - 'a' + 10);
        else break;
    }
    g_esp_fault_code = code;
    PrintDebug("[ESP] Fault: %.*s (code=%d)\r\n",
               len, payload, g_esp_fault_code);
}

static void _handle_wifi_response(const char *payload, uint8_t len)
{
    if (len > 0) {
        g_esp_wifi_connected = (payload[0] == '1') ? 1 : 0;
    }
    GUI_SPI_NotifyWifiStatus(g_esp_wifi_connected ? "CONNECTED" : "DISCONNECTED");
    PrintDebug("[ESP] WiFi status: %s\r\n",
               g_esp_wifi_connected ? "connected" : "disconnected");
}

/*
 * 复合响应处理 ★ v2 重写
 *
 * 旧格式 (v1): "F1:code|F2:1"  (已废弃)
 * 新格式 (v2): "25/100|SMTing|1|150.0|00"
 *              progress|status|heater_on|temp|fault
 * 文档 §4.4 QUERY_ALL 回复
 */
static void _handle_compound_response(const char *payload, uint8_t len)
{
    PrintDebug("[ESP] Compound: %.*s\r\n", len, payload);

    /*
     * 按 '|' 分割 5 个字段: progress | status | heater_on | temp | fault
     * 仅提取故障码 (第5字段)，其余字段由网页展示，STM32 无需解析
     */
    uint8_t f = 0;
    uint8_t seg_start = 0;
    uint8_t seg_len   = 0;

    for (uint8_t i = 0; i <= len; i++) {
        if (i == len || payload[i] == '|') {
            seg_len = i - seg_start;

            if (f == 4 && seg_len >= 2) {
                /* 第5字段: 故障码 HEX */
                uint8_t code = 0;
                for (uint8_t j = 0; j < 2 && j < seg_len; j++) {
                    char c = payload[seg_start + j];
                    code <<= 4;
                    if (c >= '0' && c <= '9')      code |= (uint8_t)(c - '0');
                    else if (c >= 'A' && c <= 'F') code |= (uint8_t)(c - 'A' + 10);
                    else if (c >= 'a' && c <= 'f') code |= (uint8_t)(c - 'a' + 10);
                }
                g_esp_fault_code = code;
            }

            f++;
            seg_start = i + 1;
        }
    }
}