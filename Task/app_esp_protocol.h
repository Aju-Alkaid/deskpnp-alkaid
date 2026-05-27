#ifndef __APP_ESP_PROTOCOL_H
#define __APP_ESP_PROTOCOL_H

/**
 * @file    app_esp_protocol.h
 * @brief   ESP32 SPI 通信协议层 — 组包 / 解包 / 命令码定义
 * @note    遵循 ESP32-C3通信接口规范_v2.0.md 中定义的 128 字节数据包格式
 *          版本: v2
 */

#include <stdint.h>

/* ================================================================
 *  主命令码
 * ================================================================ */
#define ESP_CMD_DATA_UPDATE    0x10   /* 数据更新 */
#define ESP_CMD_SYS_CONTROL    0x20   /* 系统控制 */
#define ESP_CMD_STATUS_QUERY   0x30   /* 状态查询 */
#define ESP_CMD_HEARTBEAT      0x00   /* 心跳 / 无操作 (仅读取 ESP 响应) */

/* ================================================================
 *  数据更新子命令 (主命令 0x10)
 * ================================================================ */
#define ESP_SUB_PROGRESS       0x01   /* 贴片进度 "当前数/总数"      */
#define ESP_SUB_SMT_STATUS     0x02   /* 贴片状态枚举字符串          */
#define ESP_SUB_HEATER_STATE   0x03   /* 加热台状态 "1"/"0"         */
#define ESP_SUB_HEATER_TEMP    0x04   /* 加热台温度 "85.3"          */
/* 0x05~0x0F 保留 */
/* 0x10~0x1F 保留 (扩展区) */

/* ================================================================
 *  系统控制子命令 (主命令 0x20)
 * ================================================================ */
#define ESP_SUB_WIFI_ON        0x01   /* 打开 WiFi 功能              */
#define ESP_SUB_WIFI_OFF       0x02   /* 关闭 WiFi 功能              */
/* 0x03~0x0F 保留 */

/* ================================================================
 *  状态查询子命令 (主命令 0x30)
 * ================================================================ */
#define ESP_SUB_QUERY_FAULT    0x01   /* 查询故障码                  */
#define ESP_SUB_QUERY_WIFI     0x02   /* 查询 WiFi 状态              */
#define ESP_SUB_QUERY_ALL      0x03   /* 查询全部状态 (复合)         */
/* 0x04~0x0F 保留 */

/* ================================================================
 *  ESP 响应类型 (ESP -> 主控)
 * ================================================================ */
#define ESP_RESP_IDLE          0x00   /* 空闲 / 无响应               */
#define ESP_RESP_FAULT         0xF1   /* 故障报告                    */
#define ESP_RESP_WIFI_STATUS   0xF2   /* WiFi 状态                   */
#define ESP_RESP_COMPOUND      0xFF   /* 复合状态                    */
#define ESP_RESP_VERSION       0xFE   /* 协议版本 (预留)             */

/* ================================================================
 *  故障码
 * ================================================================ */
#define ESP_FAULT_NONE         0x00   /* 无故障                      */
#define ESP_FAULT_WIFI_FAIL    0x01   /* WiFi 连接失败               */
#define ESP_FAULT_WEB_FAIL     0x02   /* Web 服务器启动失败          */
#define ESP_FAULT_SPI_CRC      0x03   /* SPI 数据 CRC 错误 (预留)    */
#define ESP_FAULT_OOM          0x04   /* 内存不足                    */
#define ESP_FAULT_TIMEOUT      0x05   /* 通信超时 (ST 端本地定义)    */

/* ================================================================
 *  贴片状态枚举字符串
 * ================================================================ */
/* 这些字符串直接发送给 ESP，由其映射到 Web 展示 */

/* ================================================================
 *  组包函数 (ST -> ESP)
 * ================================================================ */

/**
 * @brief  构建数据更新包 (主命令 0x10)
 * @param  packet       输出 128 字节缓冲区 (调用者提供)
 * @param  sub_cmd      子命令码
 * @param  payload      数据负载 (ASCII 字符串，可为 NULL)
 * @param  payload_len  负载字节数 (0~123)
 * @note   填充规则:
 *         Byte 0    = ESP_CMD_DATA_UPDATE
 *         Byte 1    = sub_cmd
 *         Byte 2    = payload_len
 *         Byte 3~125 = payload + 0x00 填充
 *         Byte 126   = 序列号 (自动递增)
 *         Byte 127   = 0x00
 */
void ESP_BuildDataPacket(uint8_t *packet, uint8_t sub_cmd,
                         const char *payload, uint8_t payload_len);

/**
 * @brief  构建系统控制包 (主命令 0x20)
 * @param  packet   输出 128 字节缓冲区
 * @param  sub_cmd  子命令 (ESP_SUB_WIFI_ON / ESP_SUB_WIFI_OFF 等)
 * @note   负载长度为 0
 */
void ESP_BuildControlPacket(uint8_t *packet, uint8_t sub_cmd);

/**
 * @brief  构建状态查询包 (主命令 0x30)
 * @param  packet   输出 128 字节缓冲区
 * @param  sub_cmd  子命令 (ESP_SUB_QUERY_FAULT / ... )
 */
void ESP_BuildQueryPacket(uint8_t *packet, uint8_t sub_cmd);

/**
 * @brief  构建心跳包 (全零，仅读取 ESP 响应)
 * @param  packet   输出 128 字节缓冲区 (全部填 0x00)
 */
void ESP_BuildHeartbeatPacket(uint8_t *packet);

/* ================================================================
 *  解包函数 (ESP -> ST)
 * ================================================================ */

/**
 * @brief  提取 ESP 响应类型
 * @param  rx_buf  ESP 发来的 128 字节接收缓冲区
 * @return Byte 0: 响应类型 (ESP_RESP_IDLE / _FAULT / _WIFI_STATUS / _COMPOUND)
 */
uint8_t ESP_GetResponseType(const uint8_t *rx_buf);

/**
 * @brief  提取 ESP 响应负载指针和长度
 * @param  rx_buf   ESP 发来的 128 字节接收缓冲区
 * @param  out_len  输出: 负载有效字节数 (从 Byte 2 读取)
 * @return 指向负载起始位置 (rx_buf + 1)，不拷贝
 */
const char* ESP_GetResponsePayload(const uint8_t *rx_buf, uint8_t *out_len);

/**
 * @brief  提取 ESP 响应的序列号
 * @param  rx_buf  ESP 发来的 128 字节接收缓冲区
 * @return Byte 126 的值
 */
uint8_t ESP_GetResponseSeq(const uint8_t *rx_buf);

/* ================================================================
 *  辅助格式化函数
 * ================================================================ */

/**
 * @brief  将温度值 (0.1°C 单位) 格式化为 "XX.X" 字符串
 * @param  buf         输出缓冲区 (至少 6 字节)
 * @param  buf_size    缓冲区大小
 * @param  temp_0_1c   温度值 (如 853 = 85.3°C)
 * @return 写入的字节数 (不含尾 0)
 * @note   不使用 sprintf，栈安全。范围 0.0 ~ 300.0
 */
int ESP_FormatTemp(char *buf, int buf_size, uint16_t temp_0_1c);

/**
 * @brief  将进度值格式化为 "cur/total" 字符串
 * @param  buf       输出缓冲区 (至少 12 字节)
 * @param  buf_size  缓冲区大小
 * @param  current   当前数
 * @param  total     总数
 * @return 写入的字节数 (不含尾 0)
 * @note   示例: current=32, total=50 -> "32/50"
 */
int ESP_FormatProgress(char *buf, int buf_size,
                       uint8_t current, uint8_t total);

/**
 * @brief  根据贴片机状态返回枚举字符串
 * @param  is_smt_active       1=贴片运行中
 * @param  is_downloading      1=导入中
 * @param  is_heating          1=加热中
 * @param  is_finished         1=已完毕
 * @return 常量字符串指针 ("SMTing"/"Waiting"/"Importing"/"Heating"/"Finished")
 */
const char* ESP_StateToString(uint8_t is_smt_active,
                              uint8_t is_downloading,
                              uint8_t is_heating,
                              uint8_t is_finished);

#endif
