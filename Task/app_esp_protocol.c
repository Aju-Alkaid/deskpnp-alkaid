/**
 * @file    app_esp_protocol.c
 * @brief   ESP32 SPI 通信协议层实现
 * @note    组包 / 解包均无堆分配，栈安全
 *          版本: v2
 */

#include "app_esp_protocol.h"
#include <string.h>

/* ---- 全局递增序列号 ---- */
static uint8_t s_seq_num = 0;

/* ---- 内部辅助 ---- */

/**
 * @brief  将无符号整数转为十进制字符串 (内部用)
 * @return  写入字节数
 */
static int _utoa(char *buf, unsigned int val)
{
    char tmp[6];
    int t = 0;
    if (val == 0) {
        buf[0] = '0';
        return 1;
    }
    while (val > 0) {
        tmp[t++] = (char)('0' + (val % 10));
        val /= 10;
    }
    int len = t;
    while (t--) {
        buf[len - 1 - t] = tmp[t];
    }
    return len;
}

/* ================================================================
 *  组包函数
 * ================================================================ */

static void _fill_header(uint8_t *packet, uint8_t cmd, uint8_t sub,
                         uint8_t data_len)
{
    memset(packet, 0, 128);
    packet[0]   = cmd;
    packet[1]   = sub;
    packet[2]   = data_len;
    packet[126] = s_seq_num++;
    packet[127] = 0x00;
}

void ESP_BuildDataPacket(uint8_t *packet, uint8_t sub_cmd,
                         const char *payload, uint8_t payload_len)
{
    if (payload_len > 123) payload_len = 123;
    _fill_header(packet, ESP_CMD_DATA_UPDATE, sub_cmd, payload_len);
    if (payload != NULL && payload_len > 0) {
        memcpy(&packet[3], payload, payload_len);
    }
}

void ESP_BuildControlPacket(uint8_t *packet, uint8_t sub_cmd)
{
    _fill_header(packet, ESP_CMD_SYS_CONTROL, sub_cmd, 0);
}

void ESP_BuildQueryPacket(uint8_t *packet, uint8_t sub_cmd)
{
    _fill_header(packet, ESP_CMD_STATUS_QUERY, sub_cmd, 0);
}

void ESP_BuildHeartbeatPacket(uint8_t *packet)
{
    memset(packet, 0, 128);
    packet[126] = s_seq_num++;
}

/* ================================================================
 *  解包函数
 * ================================================================ */

uint8_t ESP_GetResponseType(const uint8_t *rx_buf)
{
    return rx_buf[0];
}

const char* ESP_GetResponsePayload(const uint8_t *rx_buf, uint8_t *out_len)
{
    *out_len = rx_buf[2];
    return (const char*)(&rx_buf[1]);
}

uint8_t ESP_GetResponseSeq(const uint8_t *rx_buf)
{
    return rx_buf[126];
}

/* ================================================================
 *  辅助格式化函数
 * ================================================================ */

int ESP_FormatTemp(char *buf, int buf_size, uint16_t temp_0_1c)
{
    if (buf_size < 6) return 0;

    unsigned int int_part = temp_0_1c / 10;
    unsigned int frac_part = temp_0_1c % 10;

    int pos = _utoa(buf, int_part);
    buf[pos++] = '.';
    buf[pos++] = (char)('0' + frac_part);
    return pos;
}

int ESP_FormatProgress(char *buf, int buf_size,
                       uint8_t current, uint8_t total)
{
    if (buf_size < 12) return 0;

    int pos = _utoa(buf, current);
    buf[pos++] = '/';
    pos += _utoa(buf + pos, total);
    return pos;
}

const char* ESP_StateToString(uint8_t is_smt_active,
                              uint8_t is_downloading,
                              uint8_t is_heating,
                              uint8_t is_finished)
{
    if (is_finished)  return "Finished";
    if (is_heating)   return "Heating";
    if (is_downloading) return "Importing";
    if (is_smt_active)  return "SMTing";
    return "Waiting";
}
