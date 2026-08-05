/**
 * @file    app_esp_protocol.c
 * @brief   ESP32 SPI 通信协议层实现
 * @note    组包 / 解包均无堆分配，栈安全
 *          版本: v3.1 (新增 CMD 0x40/0x50/0x60/0x70/0x71)
 */

#include "app_esp_protocol.h"
#include <string.h>

/* ---- 全局递增序列号 ---- */
static uint8_t s_seq_num = 0;
static uint8_t s_last_built_seq = 0;

/* ---- 内部辅助 ---- */

/*
 * 将无符号整数转为十进制字符串 (内部用)
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
 *  CRC32 / CSV 负载解析
 * ================================================================ */

uint32_t ESP_CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    while (len > 0U) {
        uint32_t i;
        crc ^= (uint32_t)*data++;
        for (i = 0U; i < 8U; i++) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
        }
        len--;
    }
    return crc;
}

uint32_t ESP_CRC32_Finish(uint32_t crc)
{
    return ~crc;
}

uint32_t ESP_CRC32(const uint8_t *data, uint32_t len)
{
    return ESP_CRC32_Finish(ESP_CRC32_Update(0xFFFFFFFFU, data, len));
}

static int _csv_find_key(const uint8_t *payload, uint8_t len,
                         const char *key)
{
    uint8_t key_len = (uint8_t)strlen(key);
    uint8_t i;

    for (i = 0U; i + key_len <= len; i++) {
        if (memcmp(&payload[i], key, key_len) == 0) {
            return (int)(i + key_len);
        }
    }
    return -1;
}

static int _csv_parse_dec_u32(const uint8_t *payload, uint8_t len,
                              uint8_t pos, uint32_t *out)
{
    uint32_t value = 0U;
    uint8_t digits = 0U;

    while (pos < len && payload[pos] >= '0' && payload[pos] <= '9') {
        value = (value * 10U) + (uint32_t)(payload[pos] - '0');
        digits++;
        pos++;
    }
    if (digits == 0U) {
        return 0;
    }
    *out = value;
    return 1;
}

static int _csv_parse_hex_u32(const uint8_t *payload, uint8_t len,
                              uint8_t pos, uint32_t *out)
{
    uint32_t value = 0U;
    uint8_t digits = 0U;

    while (pos < len) {
        uint8_t c = payload[pos];
        uint32_t nibble;
        if (c >= '0' && c <= '9')       nibble = (uint32_t)(c - '0');
        else if (c >= 'A' && c <= 'F')  nibble = (uint32_t)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f')  nibble = (uint32_t)(c - 'a' + 10);
        else break;
        value = (value << 4U) | nibble;
        digits++;
        pos++;
    }
    if (digits == 0U || digits > 8U) {
        return 0;
    }
    *out = value;
    return 1;
}

uint8_t ESP_ParseCsvStart(const uint8_t *payload, uint8_t len,
                          uint32_t *out_total_len, uint16_t *out_frames,
                          uint32_t *out_crc32)
{
    uint32_t frames32 = 0U;
    int pos;

    if (payload == NULL || out_total_len == NULL ||
        out_frames == NULL || out_crc32 == NULL) {
        return 0;
    }

    pos = _csv_find_key(payload, len, "len=");
    if (pos < 0 || !_csv_parse_dec_u32(payload, len, (uint8_t)pos,
                                       out_total_len)) {
        return 0;
    }

    pos = _csv_find_key(payload, len, "frames=");
    if (pos < 0 || !_csv_parse_dec_u32(payload, len, (uint8_t)pos,
                                       &frames32)) {
        return 0;
    }
    if (frames32 > 0xFFFFU) {
        return 0;
    }
    *out_frames = (uint16_t)frames32;

    pos = _csv_find_key(payload, len, "crc32=");
    if (pos < 0 || !_csv_parse_hex_u32(payload, len, (uint8_t)pos,
                                       out_crc32)) {
        return 0;
    }

    return 1;
}

uint8_t ESP_ParseCsvEnd(const uint8_t *payload, uint8_t len,
                        uint32_t *out_crc32)
{
    int pos;

    if (payload == NULL || out_crc32 == NULL) {
        return 0;
    }

    pos = _csv_find_key(payload, len, "crc32=");
    if (pos < 0 || !_csv_parse_hex_u32(payload, len, (uint8_t)pos,
                                       out_crc32)) {
        return 0;
    }
    return 1;
}

uint16_t ESP_ParseCsvData(const uint8_t *payload, uint8_t len,
                          const uint8_t **out_data, uint8_t *out_data_len)
{
    if (payload == NULL || out_data == NULL || out_data_len == NULL) {
        if (out_data_len != NULL) *out_data_len = 0;
        return 0xFFFFU;
    }
    if (len < 2U) {
        *out_data = payload;
        *out_data_len = 0;
        return 0xFFFFU;
    }
    *out_data = payload + 2U;
    *out_data_len = (uint8_t)(len - 2U);
    return (uint16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8U));
}

/* ================================================================
 *  组包函数
 * ================================================================ */

static void _fill_header(uint8_t *packet, uint8_t cmd, uint8_t sub,
                         uint8_t data_len)
{
    uint8_t seq;
    memset(packet, 0, 128);
    seq = s_seq_num++;
    packet[0]   = cmd;
    packet[1]   = sub;
    packet[2]   = data_len;
    packet[126] = seq;
    packet[127] = 0x00;
    s_last_built_seq = seq;
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

void ESP_BuildControlPacketEx(uint8_t *packet, uint8_t sub_cmd,
                               const uint8_t *payload, uint8_t payload_len)
{
    if (payload_len > 123) payload_len = 123;
    _fill_header(packet, ESP_CMD_SYS_CONTROL, sub_cmd, payload_len);
    if (payload != NULL && payload_len > 0) {
        memcpy(&packet[3], payload, payload_len);
    }
}

void ESP_BuildControlPacket(uint8_t *packet, uint8_t sub_cmd)
{
    ESP_BuildControlPacketEx(packet, sub_cmd, NULL, 0);
}

void ESP_BuildQueryPacket(uint8_t *packet, uint8_t sub_cmd)
{
    _fill_header(packet, ESP_CMD_STATUS_QUERY, sub_cmd, 0);
}

void ESP_BuildFileNextPacket(uint8_t *packet, uint16_t next_frame)
{
    uint8_t payload[2];
    payload[0] = (uint8_t)(next_frame & 0xFFU);
    payload[1] = (uint8_t)((next_frame >> 8) & 0xFFU);
    _fill_header(packet, ESP_CMD_FILE_CTRL, ESP_SUB_FILE_NEXT, 2);
    memcpy(&packet[3], payload, 2);
}

void ESP_BuildFileResultPacket(uint8_t *packet, const char *result)
{
    uint8_t len = 0;
    if (result != NULL) {
        len = (uint8_t)strlen(result);
        if (len > 123) len = 123;
    }
    _fill_header(packet, ESP_CMD_FILE_CTRL, ESP_SUB_FILE_RESULT, len);
    if (len > 0) {
        memcpy(&packet[3], result, len);
    }
}

void ESP_BuildFileCancelAckPacket(uint8_t *packet)
{
    _fill_header(packet, ESP_CMD_FILE_CTRL, ESP_SUB_FILE_CANCEL_ACK, 0);
}

uint8_t ESP_GetSeq(const uint8_t *packet)
{
    return packet[126];
}

uint8_t ESP_GetLastBuiltSeq(void)
{
    return s_last_built_seq;
}

const uint8_t* ESP_GetPayloadRaw(const uint8_t *rx_buf, uint8_t *out_len)
{
    if (out_len != NULL) {
        *out_len = rx_buf[2];
    }
    return (const uint8_t*)&rx_buf[3];
}

void ESP_BuildHeartbeatPacket(uint8_t *packet)
{
    uint8_t seq;
    memset(packet, 0, 128);
    seq = s_seq_num++;
    packet[126] = seq;
    s_last_built_seq = seq;
}

/*
 * 构建日志发送包 (主命令 0x50, SUBCMD=0x01)
 * 文档 v3.1 §4.7: STM32 每执行一步发送一条日志，
 * ESP32 收到后缓存到 logBuffer，下一次云端上报打包进 JSON
 */
void ESP_BuildLogPacket(uint8_t *packet, const char *text)
{
    uint8_t len = 0;
    if (text != NULL) {
        len = (uint8_t)strlen(text);
        if (len > 123) len = 123;
    }
    _fill_header(packet, ESP_CMD_LOG_DATA, ESP_SUB_LOG_TEXT, len);
    if (len > 0) {
        memcpy(&packet[3], text, len);
    }
}

/* ================================================================
 *  解包函数
 * ================================================================ */

uint8_t ESP_GetResponseType(const uint8_t *rx_buf)
{
    return rx_buf[1];
}

const char* ESP_GetResponsePayload(const uint8_t *rx_buf, uint8_t *out_len)
{
    *out_len = rx_buf[2];
    return (const char*)(&rx_buf[3]);
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
