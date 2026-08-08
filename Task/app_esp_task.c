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
#include "app_host.h"
#include "driver_spiflash_w25q64.h"
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
static int16_t  s_last_temp;         /* 上次发送的温度 (0.1°C) */
static uint8_t  s_last_heater_state_log = 0xFFU;

/* CSV upload */
#define ESP_CSV_FLASH_SIZE        (128U * 1024U)
#define ESP_CSV_MAX_FILE_SIZE     (100U * 1024U)
#define ESP_CSV_SECTOR_SIZE       4096U
#define ESP_CSV_SESSION_TIMEOUT_MS 30000U
#define ESP_CSV_RETRY_MAX         3U
#define ESP_CSV_NEXT_RETRY_MAX    20U
#define ESP_CSV_STATE_IDLE        0
#define ESP_CSV_STATE_RECEIVING   1
#define ESP_SEQ_SLOT_COUNT        8

/* CSV upload session */
static uint8_t  s_csv_state;
static uint32_t s_csv_total_len;
static uint32_t s_csv_received;
static uint16_t s_csv_expected_frame;
static uint16_t s_csv_total_frames;
static uint32_t s_csv_running_crc;
static uint32_t s_csv_last_sector;
static uint32_t s_csv_last_frame_tick;
static uint8_t  s_csv_result_pending;
static uint8_t  s_csv_result_retries;
static uint32_t s_csv_result_retry_tick;
static uint8_t  s_csv_next_pending;
static uint8_t  s_csv_next_retries;
static uint32_t s_csv_next_retry_tick;

/* Scene B re-entrancy guard */
static uint8_t s_in_scene_b;
static uint16_t s_unknown_cmd_count;
static uint16_t s_irq_busy_log_count;

/* 8-slot SEQ mapping */
typedef struct {
    uint8_t seq;
    uint8_t cmd;
    uint8_t sub;
    uint8_t used;
} ESP_SeqSlot_t;
static ESP_SeqSlot_t s_seq_slots[ESP_SEQ_SLOT_COUNT];
static uint8_t s_seq_slot_next;

/* ---- 数据字段轮询表 ---- */
#define RR_PROGRESS       0
#define RR_SMT_STATUS     1
#define RR_HEATER_STATE   2
#define RR_HEATER_TEMP    3
#define RR_COUNT          4

static uint8_t _heater_is_active(const HeaterStatus_t *hs)
{
    if (hs == NULL) return 0;
    return (hs->state == HEATER_STATE_HEATING ||
            hs->state == HEATER_STATE_HOLDING) ? 1U : 0U;
}

/* ================================================================
 *  内部函数声明
 * ================================================================ */

static void _send_data_field(uint8_t field_id);
static void _process_control_cmd(ESP_Cmd_t cmd);
static void _process_response(void);
static void _handle_fault_response(const char *payload, uint8_t len);
static void _handle_wifi_response(const char *payload, uint8_t len);
static void _handle_compound_response(const char *payload, uint8_t len);

static void _csv_handle_frame(const uint8_t *rx);
static void _csv_reset_session(void);
static void _csv_send_result(const char *result);
static uint8_t _csv_send_next(uint16_t next_frame);
static void _csv_send_cancel_ack(void);
static uint8_t _seq_register(uint8_t cmd, uint8_t sub);
static void _seq_remove(uint8_t index);
static uint8_t _seq_find(uint8_t seq);

/* ★ v2 新增 */
static void _scene_b_read(void);       /* 场景 B: 读取 ESP→STM32 命令 */
static void _check_irq_and_read(void); /* IRQ 互斥检查 + 场景 B 执行  */
static void _process_rx(const uint8_t *rx);
static void _handle_esp_rx(const uint8_t *rx);
static uint8_t _spi_send_scene_a(uint8_t *tx, uint8_t allow_csv_frame);

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
 *  Host_Task 在执行 SMT 步骤时调用，非阻塞入队。
 *  ESP_Task 主循环统一构建 0x50/0x01 帧并执行 SPI 收发。
 * ================================================================ */
void ESP_SendLog(const char *text)
{
    ESP_LogMsg_t msg;
    size_t len;

    if (text == NULL || esp_log_queue == NULL) return;

    len = strlen(text);
    if (len > sizeof(msg.text)) len = sizeof(msg.text);
    msg.len = (uint8_t)len;
    memcpy(msg.text, text, len);

    osMessageQueuePut(esp_log_queue, &msg, 0, 0);
}

void ESP_SendWifiConnect(const char *ssid, uint16_t ssid_len,
                         const char *password, uint16_t password_len)
{
    ESP_WifiCfgMsg_t msg;

    if (ssid == NULL || password == NULL || esp_wifi_cfg_queue == NULL) return;
    if (ssid_len == 0U || ssid_len > 32U ||
        password_len < 8U || password_len > 63U) return;

    msg.len = (uint8_t)(ssid_len + 1U + password_len);
    memcpy(msg.payload, ssid, ssid_len);
    msg.payload[ssid_len] = 0;
    memcpy(&msg.payload[ssid_len + 1U], password, password_len);
    osMessageQueuePut(esp_wifi_cfg_queue, &msg, 0, 0);
}

/* 统一分发 ESP→STM32 帧，场景 B 与场景 A MISO 共用。 */
static void _handle_esp_rx(const uint8_t *rx)
{
    uint8_t cmd = ESP_GetCmd(rx);
    uint8_t sub = ESP_GetSubCmd(rx);
    uint8_t handled = 1;
    ESP_Cmd_t web_cmd;

    if (cmd == ESP_CMD_CSV_UPLOAD) {
        _csv_handle_frame(rx);
    }
    else if (cmd == ESP_CMD_PROCESS_CTRL) {
        switch (sub) {
            case ESP_SUB_PROC_START:  g_esp_web_cmd = ESP_CMD_PROC_START;  break;
            case ESP_SUB_PROC_PAUSE:  g_esp_web_cmd = ESP_CMD_PROC_PAUSE;  break;
            case ESP_SUB_PROC_RESUME: g_esp_web_cmd = ESP_CMD_PROC_RESUME; break;
            case ESP_SUB_PROC_STOP:   g_esp_web_cmd = ESP_CMD_PROC_STOP;   break;
            case ESP_SUB_PROC_ESTOP:  g_esp_web_cmd = ESP_CMD_PROC_ESTOP;  break;
            default: handled = 0; break;
        }
        if (handled) {
            g_esp_web_cmd_pending = 1;
            web_cmd = g_esp_web_cmd;
            if (esp_web_cmd_queue != NULL) {
                osMessageQueuePut(esp_web_cmd_queue, &web_cmd, 0, 0);
            }
            PrintDebug("[ESP] Web PROC cmd 0x%02X\r\n", sub);
        }
    }
    else if (cmd == ESP_CMD_HEATER_CTRL) {
        if (sub == ESP_SUB_HEAT_START)      g_esp_web_cmd = ESP_CMD_HEAT_START;
        else if (sub == ESP_SUB_HEAT_STOP)  g_esp_web_cmd = ESP_CMD_HEAT_STOP;
        else handled = 0;
        if (handled) {
            g_esp_web_cmd_pending = 1;
            web_cmd = g_esp_web_cmd;
            if (esp_web_cmd_queue != NULL) {
                osMessageQueuePut(esp_web_cmd_queue, &web_cmd, 0, 0);
            }
            PrintDebug("[ESP] Web HEATER cmd 0x%02X\r\n", sub);
        }
    }
    else if (cmd == ESP_CMD_HEARTBEAT) {
        _process_rx(rx);
    }
    else {
        s_unknown_cmd_count++;
        if (s_unknown_cmd_count <= 5U ||
            (s_unknown_cmd_count % 100U) == 0U) {
            PrintDebug("[ESP] Unknown ESP cmd 0x%02X sub 0x%02X\r\n", cmd, sub);
        }
    }
}

static void _scene_b_read(void)
{
    uint8_t dummy[128];
    uint8_t rx[128];

    s_in_scene_b = 1;

    memset(dummy, 0xFF, 128);
    if (ESP_SPI_Transfer(dummy, rx) != HAL_OK) {
        PrintDebug("[ESP] Scene B SPI error/timeout\r\n");
        esp32_irq_flag = 0;
        s_in_scene_b = 0;
        return;
    }

    osDelay(2);
    esp32_irq_flag = 0;

    _handle_esp_rx(rx);

    s_in_scene_b = 0;
}

/*
 * IRQ 互斥检查: 检查 esp32_irq_flag 和引脚电平
 * 若 IRQ 为低则优先执行场景 B
 */
static void _check_irq_and_read(void)
{
    if (s_in_scene_b) return;
    if (esp32_irq_flag || !ESP_CheckIRQ()) {
        _scene_b_read();
    }
}

/*
 * 场景 A: 主动下发前先让场景 B 优先执行；
 * IRQ 恢复高电平后才发送，发送后延时 2ms 并检查下一轮响应/IRQ。
 */
static uint8_t _spi_send_scene_a(uint8_t *tx, uint8_t allow_csv_frame)
{
    uint8_t attempts;

    for (attempts = 0; attempts < 10U; attempts++) {
        _check_irq_and_read();
        if (!allow_csv_frame && s_csv_state == ESP_CSV_STATE_RECEIVING) {
            return 0;
        }
        if (ESP_CheckIRQ()) break;
        osDelay(1);
    }

    if (!ESP_CheckIRQ()) {
        s_irq_busy_log_count++;
        if (s_irq_busy_log_count <= 5U ||
            (s_irq_busy_log_count % 100U) == 0U) {
            PrintDebug("[ESP] IRQ busy, scene A frame dropped\r\n");
        }
        return 0;
    }
    if (!allow_csv_frame && s_csv_state == ESP_CSV_STATE_RECEIVING) {
        return 0;
    }

    if (ESP_SPI_Transfer(tx, s_rx_buf) != HAL_OK) {
        PrintDebug("[ESP] Scene A SPI error/timeout\r\n");
        return 0;
    }

    osDelay(2);
    if (ESP_IsEspToStmCmd(s_rx_buf)) {
        _handle_esp_rx(s_rx_buf);
    } else {
        _process_response();
    }
    _check_irq_and_read();
    return 1;
}

static uint8_t _seq_find(uint8_t seq)
{
    uint8_t i;
    for (i = 0; i < ESP_SEQ_SLOT_COUNT; i++) {
        if (s_seq_slots[i].used && s_seq_slots[i].seq == seq) {
            return i;
        }
    }
    return 0xFFU;
}

static void _seq_remove(uint8_t index)
{
    if (index < ESP_SEQ_SLOT_COUNT) {
        s_seq_slots[index].used = 0;
    }
}

static uint8_t _seq_register(uint8_t cmd, uint8_t sub)
{
    uint8_t i;
    uint8_t seq = ESP_GetLastBuiltSeq();

    for (i = 0; i < ESP_SEQ_SLOT_COUNT; i++) {
        if (!s_seq_slots[i].used) {
            s_seq_slots[i].seq = seq;
            s_seq_slots[i].cmd = cmd;
            s_seq_slots[i].sub = sub;
            s_seq_slots[i].used = 1;
            return i;
        }
    }

    i = s_seq_slot_next++;
    s_seq_slot_next %= ESP_SEQ_SLOT_COUNT;
    s_seq_slots[i].seq = seq;
    s_seq_slots[i].cmd = cmd;
    s_seq_slots[i].sub = sub;
    s_seq_slots[i].used = 1;
    return i;
}

static uint8_t _seq_slot_matches_response(uint8_t slot, uint8_t resp_type)
{
    if (slot >= ESP_SEQ_SLOT_COUNT || !s_seq_slots[slot].used) {
        return 0;
    }
    if (s_seq_slots[slot].cmd != ESP_CMD_STATUS_QUERY) {
        return 0;
    }
    switch (resp_type) {
    case ESP_RESP_FAULT:
        return (s_seq_slots[slot].sub == ESP_SUB_QUERY_FAULT) ? 1U : 0U;
    case ESP_RESP_WIFI_STATUS:
        return (s_seq_slots[slot].sub == ESP_SUB_QUERY_WIFI) ? 1U : 0U;
    case ESP_RESP_COMPOSITE:
        return (s_seq_slots[slot].sub == ESP_SUB_QUERY_ALL) ? 1U : 0U;
    case ESP_RESP_VERSION:
        return 1U;
    default:
        return 0U;
    }
}

static void _csv_reset_session(void)
{
    s_csv_state = ESP_CSV_STATE_IDLE;
    s_csv_total_len = 0;
    s_csv_total_frames = 0;
    s_csv_received = 0;
    s_csv_expected_frame = 0;
    s_csv_running_crc = 0xFFFFFFFFU;
    s_csv_last_sector = 0xFFFFFFFFU;
    s_csv_result_pending = 0;
    s_csv_result_retries = 0;
    s_csv_result_retry_tick = 0;
    s_csv_next_pending = 0;
    s_csv_next_retries = 0;
    s_csv_next_retry_tick = 0;
}

static uint8_t _csv_spi_send(uint8_t *tx)
{
    return _spi_send_scene_a(tx, 1);
}

static void _csv_send_result(const char *result)
{
    uint8_t tx[128];
    uint8_t ok;
    ESP_BuildFileResultPacket(tx, result);
    ok = _csv_spi_send(tx);
    PrintDebug("[ESP] CSV RESULT=%s %s\r\n",
               result, ok ? "OK" : "FAIL");
}

static uint8_t _csv_send_next(uint16_t next_frame)
{
    uint8_t tx[128];
    uint8_t ok;
    ESP_BuildFileNextPacket(tx, next_frame);
    PrintDebug("[ESP] CSV NEXT TX: %02X %02X %02X %02X %02X\r\n",
               tx[0], tx[1], tx[2], tx[3], tx[4]);
    ok = _csv_spi_send(tx);
    PrintDebug("[ESP] CSV NEXT=%u %s\r\n",
               (unsigned)next_frame, ok ? "OK" : "FAIL");
    return ok;
}

static void _csv_send_cancel_ack(void)
{
    uint8_t tx[128];
    ESP_BuildFileCancelAckPacket(tx);
    _csv_spi_send(tx);
}

static void _csv_handle_frame(const uint8_t *rx)
{
    uint8_t sub = ESP_GetSubCmd(rx);
    uint8_t len;
    const uint8_t *payload = ESP_GetPayloadRaw(rx, &len);

    if (len > ESP_PAYLOAD_MAX) {
        _csv_reset_session();
        _csv_send_result("fail:4");
        return;
    }
    PrintDebug("[ESP] CSV RX CMD=%02X SUB=%02X LEN=%u",
               ESP_GetCmd(rx), sub, (unsigned)len);
    for (uint8_t i = 0; i < len && i < 8; i++) {
        PrintDebug(" %02X", rx[ESP_OFF_PAYLOAD + i]);
    }
    PrintDebug("\r\n");
    s_csv_last_frame_tick = osKernelGetTickCount();
    s_csv_result_pending = 0;
    s_csv_next_pending = 0;

    if (sub == ESP_SUB_CSV_CANCEL) {
        _csv_reset_session();
        _csv_send_cancel_ack();
        return;
    }

    if (sub == ESP_SUB_CSV_START) {
        uint32_t total_len = 0;
        uint16_t frames = 0;
        uint32_t crc32 = 0;
        uint32_t expect_frames;

        _csv_reset_session();

        if (ESP_ParseCsvStart(payload, len, &total_len, &frames, &crc32) == 0 ||
            total_len > ESP_CSV_MAX_FILE_SIZE ||
            total_len > ESP_CSV_FLASH_SIZE) {
            _csv_send_result("fail:4");
            return;
        }

        expect_frames = (total_len + 120U) / 121U;
        if (frames != expect_frames) {
            _csv_send_result("fail:4");
            return;
        }

        s_csv_state = ESP_CSV_STATE_RECEIVING;
        s_csv_total_len = total_len;
        s_csv_total_frames = frames;
        _csv_send_result("ok");
        s_csv_result_pending = 1;
        s_csv_result_retries = 0;
        s_csv_result_retry_tick = osKernelGetTickCount();
        return;
    }

    if (s_csv_state != ESP_CSV_STATE_RECEIVING) {
        _csv_reset_session();
        _csv_send_result("fail:3");
        return;
    }

    if (sub == ESP_SUB_CSV_DATA) {
        uint16_t frame;
        uint8_t data_len;
        const uint8_t *data;
        uint32_t new_received;
        uint32_t write_addr;
        uint32_t remain;

        if (len < 2U) {
            _csv_reset_session();
            _csv_send_result("fail:3");
            return;
        }

        frame = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));
        data_len = (uint8_t)(len - 2U);
        if (data_len > 121U) {
            _csv_reset_session();
            _csv_send_result("fail:3");
            return;
        }
        data = payload + 2U;
        new_received = s_csv_received + data_len;

        if (s_csv_expected_frame > 0 &&
            frame == (uint16_t)(s_csv_expected_frame - 1U) &&
            s_csv_received > 0) {
            PrintDebug("[ESP] CSV duplicate DATA %u, resend NEXT %u\r\n",
                       (unsigned)frame, (unsigned)s_csv_expected_frame);
            if (!_csv_send_next(s_csv_expected_frame)) {
                s_csv_next_pending = 1;
                s_csv_next_retries = 0;
                s_csv_next_retry_tick = osKernelGetTickCount();
            }
            return;
        }

        if (frame != s_csv_expected_frame) {
            _csv_reset_session();
            _csv_send_result("fail:3");
            return;
        }

        if (new_received > s_csv_total_len ||
            new_received > ESP_CSV_MAX_FILE_SIZE ||
            new_received > ESP_CSV_FLASH_SIZE) {
            _csv_reset_session();
            _csv_send_result("fail:4");
            return;
        }

        write_addr = ESP_CSV_FLASH_BASE + s_csv_received;
        remain = data_len;
        while (remain > 0U) {
            uint32_t sector = write_addr / ESP_CSV_SECTOR_SIZE;
            uint32_t chunk;
            if (sector != s_csv_last_sector) {
                if (W25Q64_Erase(sector * ESP_CSV_SECTOR_SIZE,
                                 ESP_CSV_SECTOR_SIZE) < 0) {
                    _csv_reset_session();
                    _csv_send_result("fail:4");
                    return;
                }
                s_csv_last_sector = sector;
            }
            chunk = ESP_CSV_SECTOR_SIZE -
                    (write_addr % ESP_CSV_SECTOR_SIZE);
            if (chunk > remain) chunk = remain;
            if (W25Q64_Write(write_addr, (uint8_t *)data, chunk) < 0) {
                _csv_reset_session();
                _csv_send_result("fail:4");
                return;
            }
            write_addr += chunk;
            data += chunk;
            remain -= chunk;
        }

        s_csv_running_crc = ESP_CRC32_Update(s_csv_running_crc,
                                             payload + 2U, data_len);
        s_csv_received = new_received;
        s_csv_expected_frame++;
        if (!_csv_send_next(s_csv_expected_frame)) {
            s_csv_next_pending = 1;
            s_csv_next_retries = 0;
            s_csv_next_retry_tick = osKernelGetTickCount();
        }
        return;
    }

    if (sub == ESP_SUB_CSV_END) {
        uint32_t crc32 = 0;
        uint32_t total_len;

        if (ESP_ParseCsvEnd(payload, len, &crc32) == 0) {
            _csv_reset_session();
            _csv_send_result("fail:2");
            return;
        }

        if (s_csv_received != s_csv_total_len ||
            s_csv_expected_frame != s_csv_total_frames) {
            _csv_reset_session();
            _csv_send_result("fail:2");
            return;
        }

        if (ESP_CRC32_Finish(s_csv_running_crc) != crc32) {
            _csv_reset_session();
            _csv_send_result("fail:1");
            return;
        }

        total_len = s_csv_total_len;
        _csv_reset_session();
        if (esp_csv_import_queue != NULL) {
            uint32_t msg = total_len;
            if (osMessageQueuePut(esp_csv_import_queue, &msg,
                                  0U, pdMS_TO_TICKS(50)) != osOK) {
                _csv_send_result("fail:4");
                return;
            }
        }
        _csv_send_result("ok");
        return;
    }

    PrintDebug("[ESP] Unknown CSV sub=0x%02X\r\n", sub);
}

/* ================================================================
 *  ESP_Task — 任务主循环 ★ v2 重写
 * ================================================================ */
void ESP_Task(void *argument)
{
    ESP_Cmd_t cmd;
    ESP_LogMsg_t log_msg;

    /* ---- 初始化 ---- */
    ESP_GPIO_Init();                  /* CS=HIGH, IRQ EXTI (v3.1 无 RST) */
    osDelay(200);                     /* 等待 ESP32 SPI 从机就绪 */

    PrintDebug("[ESP] Task started v3.1 (IRQ enabled, waiting for WiFi open)\r\n");

    /* ---- 主循环 ---- */
    for (;;) {
        /*
         * 步骤1: 优先检查 IRQ 标志/引脚 (场景 B)
         * 在阻塞等待前先处理可能已经到来的网页命令
         */
        _check_irq_and_read();

        if (s_csv_state == ESP_CSV_STATE_RECEIVING) {
            while (s_csv_state == ESP_CSV_STATE_RECEIVING) {
                _check_irq_and_read();
                if (s_csv_state != ESP_CSV_STATE_RECEIVING) break;
                if (s_csv_result_pending &&
                    (osKernelGetTickCount() - s_csv_result_retry_tick) >=
                    pdMS_TO_TICKS(1000)) {
                    s_csv_result_retry_tick = osKernelGetTickCount();
                    if (s_csv_result_retries < ESP_CSV_RETRY_MAX) {
                        s_csv_result_retries++;
                        _csv_send_result("ok");
                    } else {
                        PrintDebug("[ESP] CSV RESULT retry exhausted, wait timeout\r\n");
                        s_csv_result_pending = 0;
                    }
                }
                if (s_csv_next_pending &&
                    (osKernelGetTickCount() - s_csv_next_retry_tick) >=
                    pdMS_TO_TICKS(1000)) {
                    s_csv_next_retry_tick = osKernelGetTickCount();
                    if (s_csv_next_retries < ESP_CSV_NEXT_RETRY_MAX) {
                        s_csv_next_retries++;
                        _csv_send_next(s_csv_expected_frame);
                    } else {
                        PrintDebug("[ESP] CSV NEXT retry exhausted, wait timeout\r\n");
                        s_csv_next_pending = 0;
                    }
                }
                if ((osKernelGetTickCount() - s_csv_last_frame_tick) >=
                    ESP_CSV_SESSION_TIMEOUT_MS) {
                    PrintDebug("[ESP] CSV session timeout, reset\r\n");
                    _csv_reset_session();
                    _csv_send_result("fail:4");
                    break;
                }
                osDelay(1);
            }
            continue;
        }

        /*
         * 步骤2: 阻塞等待 500ms 轮询超时 或 esp_cmd_queue 有新命令
         */
        osThreadFlagsWait(0x01, osFlagsWaitAny, pdMS_TO_TICKS(500));

        /*
         * 步骤3: 优先处理控制命令队列 (不阻塞，一次处理所有积压)
         */
        while (osMessageQueueGet(esp_cmd_queue, &cmd, NULL, 0) == osOK) {
            _process_control_cmd(cmd);
        }

        /* v3.1 0x20/0x03: 下发 SSID\0PASSWORD 凭据 */
        {
            ESP_WifiCfgMsg_t wifi_cfg;
            while (esp_wifi_cfg_queue != NULL &&
                   osMessageQueueGet(esp_wifi_cfg_queue, &wifi_cfg, NULL, 0) == osOK) {
                ESP_BuildControlPacketEx(s_tx_buf, ESP_SUB_WIFI_CONNECT,
                                         wifi_cfg.payload, wifi_cfg.len);
                if (_spi_send_scene_a(s_tx_buf, 0)) {
                    g_esp_wifi_enabled = 1;
                }
            }
        }

        for (;;) {
            uint8_t text[124];
            uint8_t copy_len;

            _check_irq_and_read();
            if (s_csv_state == ESP_CSV_STATE_RECEIVING) break;
            if (osMessageQueueGet(esp_log_queue, &log_msg, NULL, 0) != osOK) break;

            copy_len = log_msg.len;
            if (copy_len > 123) copy_len = 123;
            memcpy(text, log_msg.text, copy_len);
            text[copy_len] = 0;
            ESP_BuildLogPacket(s_tx_buf, (const char *)text);
            _spi_send_scene_a(s_tx_buf, 0);
        }

        /*
         * 步骤4: 轮询超时 → 周期数据发送 (场景 A)
         */
        _send_data_field(s_round_robin_index);
        s_round_robin_index++;
        if (s_round_robin_index >= RR_COUNT) {
            s_round_robin_index = 0;
        }

        _spi_send_scene_a(s_tx_buf, 0);
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
            uint8_t is_heating = _heater_is_active(&hs);
            const char *state_str = ESP_StateToString(
                if_now_SMT, if_DOWNLOAD_READY, is_heating,
                Host_IsSmtFinished());
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
            uint8_t is_on = _heater_is_active(&hs);
            payload[0] = is_on ? '1' : '0';
            ESP_BuildDataPacket(s_tx_buf, ESP_SUB_HEATER_STATE,
                                payload, 1);
            if (payload[0] != s_last_heater_state_log) {
                PrintDebug("[ESP] HEATER_STATE=%u\r\n", (unsigned)is_on);
                s_last_heater_state_log = payload[0];
            }
        }
        break;

    case RR_HEATER_TEMP:
        {
            HeaterStatus_t hs = Heater_GetCurrentStatus();
            int16_t cur_temp = hs.cur_temp;

            int32_t diff = (int32_t)cur_temp - (int32_t)s_last_temp;
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
        if (_spi_send_scene_a(s_tx_buf, 0)) {
            g_esp_wifi_enabled = 1;
        }
        break;

    case ESP_CMD_WIFI_OFF:
        PrintDebug("[ESP] WiFi OFF command\r\n");
        ESP_BuildControlPacket(s_tx_buf, ESP_SUB_WIFI_OFF);
        if (_spi_send_scene_a(s_tx_buf, 0)) {
            g_esp_wifi_enabled  = 0;
            g_esp_wifi_connected = 0;
        }
        break;

    case ESP_CMD_CS_HIGH_TEST:
        HAL_GPIO_WritePin(SPI4_CS_GPIO_Port, SPI4_CS_Pin, GPIO_PIN_SET);
        PrintDebug("[ESP] CS high test: hold PE3 HIGH 30s, no SPI\r\n");
        osDelay(pdMS_TO_TICKS(30000U));
        s_rx_buf[0] = 0xFF; /* prevent stale response processing */
        PrintDebug("[ESP] CS high test done\r\n");
        break;

    case ESP_CMD_QUERY_FAULT:
        ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_FAULT);
        {
            uint8_t slot = _seq_register(ESP_CMD_STATUS_QUERY, ESP_SUB_QUERY_FAULT);
            if (!_spi_send_scene_a(s_tx_buf, 0)) {
                _seq_remove(slot);
            }
        }
        break;

    case ESP_CMD_QUERY_WIFI:
        ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_WIFI);
        {
            uint8_t slot = _seq_register(ESP_CMD_STATUS_QUERY, ESP_SUB_QUERY_WIFI);
            if (!_spi_send_scene_a(s_tx_buf, 0)) {
                _seq_remove(slot);
            }
        }
        break;

    case ESP_CMD_QUERY_ALL:     /* ★ v2 新增 */
        ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_ALL);
        {
            uint8_t slot = _seq_register(ESP_CMD_STATUS_QUERY, ESP_SUB_QUERY_ALL);
            if (!_spi_send_scene_a(s_tx_buf, 0)) {
                _seq_remove(slot);
            }
        }
        break;

    default:
        break;
    }
}

/* ================================================================
 *  ESP 响应处理
 * ================================================================ */
static void _process_rx(const uint8_t *rx)
{
    uint8_t resp_type;
    uint8_t resp_seq;
    uint8_t slot;
    uint8_t payload_len;
    const char *payload;

    /* 防御 MISO 浮空: 有效响应的 Byte 0 固定为 0x00 */
    if (rx[0] != 0x00) {
        return;
    }

    resp_type = ESP_GetResponseType(rx);

    if (resp_type == ESP_RESP_IDLE) {
        s_no_resp_count = 0;
        return;
    }

    if (resp_type != ESP_RESP_FAULT &&
        resp_type != ESP_RESP_WIFI_STATUS &&
        resp_type != ESP_RESP_COMPOSITE &&
        resp_type != ESP_RESP_VERSION) {
        PrintDebug("[ESP] Ignore response type 0x%02X seq 0x%02X\r\n",
                   resp_type, ESP_GetResponseSeq(rx));
        return;
    }

    resp_seq = ESP_GetResponseSeq(rx);
    slot = _seq_find(resp_seq);
    if (slot == 0xFFU) {
        PrintDebug("[ESP] Ignore unmatched seq 0x%02X\r\n", resp_seq);
        return;
    }

    if (!_seq_slot_matches_response(slot, resp_type)) {
        PrintDebug("[ESP] Ignore mismatched response type 0x%02X seq 0x%02X\r\n",
                   resp_type, resp_seq);
        _seq_remove(slot);
        return;
    }

    /* payload 长度合法性检查 */
    if (rx[2] > 123) {
        return;
    }

    /* 有效响应 */
    g_esp_last_rx_tick = osKernelGetTickCount();
    s_no_resp_count = 0;

    payload = ESP_GetResponsePayload(rx, &payload_len);
    _seq_remove(slot);

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

static void _process_response(void)
{
    _process_rx(s_rx_buf);
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
