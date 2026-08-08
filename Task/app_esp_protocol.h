#ifndef __APP_ESP_PROTOCOL_H
#define __APP_ESP_PROTOCOL_H

/**
 * @file    app_esp_protocol.h
 * @brief   ESP32 SPI 通信协议层 — 组包 / 解包 / 命令码定义
 * @note    遵循 ESP32-C3 与 STM32 主控 SPI 通讯接口文档 v3.1
 *          128 字节固定帧: CMD(1)+SUBCMD(1)+LEN(1)+PAYLOAD(123)+SEQ(1)+RESERVED(1)
 */

#include <stdint.h>

/* ================================================================
 *  固定帧字段
 * ================================================================ */
#define ESP_FRAME_LEN        128
#define ESP_OFF_CMD          0
#define ESP_OFF_SUBCMD       1
#define ESP_OFF_LEN          2
#define ESP_OFF_PAYLOAD      3
#define ESP_PAYLOAD_MAX      123
#define ESP_OFF_SEQ          126
#define ESP_OFF_RESERVED     127

/* ================================================================
 *  主命令码
 * ================================================================ */
#define ESP_CMD_HEARTBEAT      0x00   /* 心跳 / 无操作 (仅读取 ESP 响应)     */
#define ESP_CMD_DATA_UPDATE    0x10   /* 数据更新 (STM32→ESP)                 */
#define ESP_CMD_SYS_CONTROL    0x20   /* 系统控制 (STM32→ESP)                 */
#define ESP_CMD_STATUS_QUERY   0x30   /* 状态查询 (STM32→ESP)                 */
#define ESP_CMD_PROCESS_CTRL   0x40   /* 贴片流程控制 (ESP→STM32)  ★ v2 新增  */
#define ESP_CMD_LOG_DATA       0x50   /* 日志文本 (STM32→ESP)    ★ v2 新增   */
#define ESP_CMD_HEATER_CTRL    0x60   /* 加热台控制 (ESP→STM32)  ★ v2 新增   */
#define ESP_CMD_CSV_UPLOAD    0x70
#define ESP_CMD_FILE_CTRL     0x71

/* ================================================================
 *  数据更新子命令 (主命令 0x10)
 * ================================================================ */
#define ESP_SUB_PROGRESS       0x01   /* 贴片进度 "当前数/总数"              */
#define ESP_SUB_SMT_STATUS     0x02   /* 贴片状态枚举字符串                  */
#define ESP_SUB_HEATER_STATE   0x03   /* 加热台状态 "1"/"0"                 */
#define ESP_SUB_HEATER_TEMP    0x04   /* 加热台温度 "85.3"                  */

/* ================================================================
 *  系统控制子命令 (主命令 0x20)
 * ================================================================ */
#define ESP_SUB_WIFI_ON        0x01   /* 打开 WiFi 功能                      */
#define ESP_SUB_WIFI_OFF       0x02   /* 关闭 WiFi 功能                      */
#define ESP_SUB_WIFI_CONNECT  0x03

/* ================================================================
 *  状态查询子命令 (主命令 0x30)
 * ================================================================ */
#define ESP_SUB_QUERY_FAULT    0x01   /* 查询故障码                          */
#define ESP_SUB_QUERY_WIFI     0x02   /* 查询 WiFi 状态                      */
#define ESP_SUB_QUERY_ALL      0x03   /* 查询全部状态 (复合)                 */

/* ================================================================
 *  贴片流程控制子命令 (主命令 0x40, ESP→STM32) ★ v2 新增
 * ================================================================ */
#define ESP_SUB_PROC_START     0x01   /* 开始: 设备回原点后从 P4 对准开始     */
#define ESP_SUB_PROC_PAUSE     0x02   /* 暂停: 电机回原点, 可选继续或结束    */
#define ESP_SUB_PROC_RESUME    0x03   /* 继续: 暂停后恢复流程                */
#define ESP_SUB_PROC_STOP      0x04   /* 结束: 结束此次任务                  */
#define ESP_SUB_PROC_ESTOP     0x05   /* 急停: 立即停止, 不再进行后续动作    */

/* ================================================================
 *  日志数据子命令 (主命令 0x50, STM32→ESP) ★ v2 新增
 * ================================================================ */
#define ESP_SUB_LOG_TEXT       0x01   /* UTF-8 日志文本 (≤123 字节)          */

/* ================================================================
 *  加热台控制子命令 (主命令 0x60, ESP→STM32) ★ v2 新增
 * ================================================================ */
#define ESP_SUB_HEAT_START     0x10   /* 开启加热                            */
#define ESP_SUB_HEAT_STOP      0x11   /* 暂停加热                            */

/* ================================================================
 *  ESP 响应类型 (ESP -> 主控, 在 rx_buf[1])
 * ================================================================ */
#define ESP_SUB_CSV_START     0x01
#define ESP_SUB_CSV_DATA      0x02
#define ESP_SUB_CSV_END       0x03
#define ESP_SUB_CSV_CANCEL    0x04
#define ESP_SUB_FILE_NEXT      0x01
#define ESP_SUB_FILE_RESULT    0x02
#define ESP_SUB_FILE_CANCEL_ACK 0x03
#define ESP_RESP_IDLE          0x00   /* 空闲 / 无响应                       */
#define ESP_RESP_FAULT         0xF1   /* 故障报告                            */
#define ESP_RESP_WIFI_STATUS   0xF2   /* WiFi 状态                           */
#define ESP_RESP_COMPOUND      0xFF   /* 复合状态 (同 ESP_RESP_COMPOSITE)     */
#define ESP_RESP_COMPOSITE     0xFF   /* 复合状态 (文档命名, v2 对齐)        */
#define ESP_RESP_VERSION       0xFE   /* 协议版本 (预留)                     */

/* ================================================================
 *  故障码
 * ================================================================ */
#define ESP_FAULT_NONE         0x00   /* 无故障                              */
#define ESP_FAULT_WIFI_FAIL    0x01   /* WiFi 连接失败                       */
#define ESP_FAULT_WEB_FAIL     0x02   /* Web 服务器启动失败                  */
#define ESP_FAULT_SPI_CRC      0x03   /* SPI 数据 CRC 错误 (预留)            */
#define ESP_FAULT_OOM          0x04   /* 内存不足                            */
#define ESP_FAULT_TIMEOUT      0x05   /* 通信超时 (ST 端本地定义)            */

/* ================================================================
 *  组包函数 (ST -> ESP)
 * ================================================================ */

/**
 * @brief  构建数据更新包 (主命令 0x10)
 * @param  packet       输出 128 字节缓冲区 (调用者提供)
 * @param  sub_cmd      子命令码
 * @param  payload      数据负载 (ASCII 字符串，可为 NULL)
 * @param  payload_len  负载字节数 (0~123)
 */
void ESP_BuildDataPacket(uint8_t *packet, uint8_t sub_cmd,
                         const char *payload, uint8_t payload_len);

/**
 * @brief  构建系统控制包 (主命令 0x20)
 */
void ESP_BuildControlPacket(uint8_t *packet, uint8_t sub_cmd);
void ESP_BuildControlPacketEx(uint8_t *packet, uint8_t sub_cmd,
                              const uint8_t *payload, uint8_t payload_len);
void ESP_BuildFileNextPacket(uint8_t *packet, uint16_t next_frame);
void ESP_BuildFileResultPacket(uint8_t *packet, const char *result);
void ESP_BuildFileCancelAckPacket(uint8_t *packet);
uint8_t ESP_GetSeq(const uint8_t *packet);
const uint8_t* ESP_GetPayloadRaw(const uint8_t *rx_buf, uint8_t *out_len);
uint8_t ESP_GetLastBuiltSeq(void);

/**
 * @brief  标准 CRC32（与 Python zlib.crc32 一致）
 */
uint32_t ESP_CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t ESP_CRC32_Finish(uint32_t crc);
uint32_t ESP_CRC32(const uint8_t *data, uint32_t len);

/**
 * @brief  解析 0x70 上传帧负载
 * @retval 1 成功，0 失败
 */
uint8_t ESP_ParseCsvStart(const uint8_t *payload, uint8_t len,
                          uint32_t *out_total_len, uint16_t *out_frames,
                          uint32_t *out_crc32);
uint8_t ESP_ParseCsvEnd(const uint8_t *payload, uint8_t len,
                        uint32_t *out_crc32);

/**
 * @brief  提取 DATA 帧号与原始字节
 * @retval 帧号；payload 长度 <2 时返回 0xFFFF
 */
uint16_t ESP_ParseCsvData(const uint8_t *payload, uint8_t len,
                          const uint8_t **out_data, uint8_t *out_data_len);

/**
 * @brief  构建状态查询包 (主命令 0x30)
 */
void ESP_BuildQueryPacket(uint8_t *packet, uint8_t sub_cmd);

/**
 * @brief  构建心跳包 (全零，仅读取 ESP 响应)
 */
void ESP_BuildHeartbeatPacket(uint8_t *packet);

/**
 * @brief  构建日志发送包 (主命令 0x50) ★ v2 新增
 * @param  packet   输出 128 字节缓冲区
 * @param  text     UTF-8 日志文本 (自动截断至 123 字节)
 */
void ESP_BuildLogPacket(uint8_t *packet, const char *text);

/* ================================================================
 *  解包函数 (ESP -> ST)
 * ================================================================ */

/** @brief 提取 ESP 响应类型 (rx_buf[1]) */
uint8_t ESP_GetResponseType(const uint8_t *rx_buf);

/** @brief 提取 ESP 响应负载指针和长度 */
const char* ESP_GetResponsePayload(const uint8_t *rx_buf, uint8_t *out_len);

/** @brief 提取 ESP 响应的序列号 (rx_buf[126]) */
uint8_t ESP_GetResponseSeq(const uint8_t *rx_buf);

/**
 * @brief  提取接收帧的主命令 (rx_buf[0]) ★ v2 新增
 * @note   用于区分 STM32→ESP (0x00/0x10/0x20/0x30/0x50)
 *         和 ESP→STM32 (0x40/0x60) 方向的命令
 */
static inline uint8_t ESP_GetCmd(const uint8_t *rx_buf) {
    return rx_buf[0];
}

/**
 * @brief  提取接收帧的子命令 (rx_buf[1]) ★ v2 新增
 */
static inline uint8_t ESP_GetSubCmd(const uint8_t *rx_buf) {
    return rx_buf[1];
}

/**
 * @brief  判断接收帧是否为 ESP→STM32 方向命令 ★ v2 新增
 * @retval 1: ESP→STM32 (0x40 流程控制 / 0x60 加热台控制)
 *         0: STM32→ESP 或空闲
 */
static inline uint8_t ESP_IsEspToStmCmd(const uint8_t *rx_buf) {
    uint8_t cmd = rx_buf[0];
    return (cmd == ESP_CMD_PROCESS_CTRL || cmd == ESP_CMD_HEATER_CTRL ||
                    cmd == ESP_CMD_CSV_UPLOAD) ? 1 : 0;
}

/* ================================================================
 *  辅助格式化函数
 * ================================================================ */

int ESP_FormatTemp(char *buf, int buf_size, int16_t temp_0_1c);
int ESP_FormatProgress(char *buf, int buf_size, uint8_t current, uint8_t total);
const char* ESP_StateToString(uint8_t is_smt_active,
                              uint8_t is_downloading,
                              uint8_t is_heating,
                              uint8_t is_finished);

#endif
