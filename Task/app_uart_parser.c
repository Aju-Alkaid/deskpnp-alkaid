#include "app_uart_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- 内部辅助函数 ---- */

static uint8_t calc_xor(const uint8_t *data, uint16_t len) {
    uint8_t cs = 0;
    for (uint16_t i = 0; i < len; i++) cs ^= data[i];
    return cs;
}

static bool parse_float(const char *s, float *out) {
    if (s == NULL || *s == '\0') return false;
    char *end = NULL;
    *out = (float)strtof(s, &end);
    return (end != s);
}

static bool parse_uint(const char *s, uint16_t *out) {
    if (s == NULL || *s == '\0') return false;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || v < 0 || v > 65535) return false;
    *out = (uint16_t)v;
    return true;
}

/* 查找命令类型 */
static PktType_t find_cmd_type(const char *cmd, uint8_t len) {
    if (len >= 5 && memcmp(cmd, "BOARD", 5) == 0) return PKT_BOARD;
    if (len >= 4 && memcmp(cmd, "COMP", 4) == 0)  return PKT_COMP;
    if (len >= 3 && memcmp(cmd, "END", 3) == 0)   return PKT_END;
    if (len >= 5 && memcmp(cmd, "READY", 5) == 0) return PKT_READY;
    if (len >= 3 && memcmp(cmd, "ACK", 3) == 0)   return PKT_ACK;
    if (len >= 3 && memcmp(cmd, "ERR", 3) == 0)   return PKT_ERR;
    if (len >= 4 && memcmp(cmd, "DONE", 4) == 0)  return PKT_DONE;
    if (len >= 2 && memcmp(cmd, "OK", 2) == 0)    return PKT_CAM_OK;
    return PKT_UNKNOWN;
}

/* 按逗号切字段，返回第 n 个字段(0-based) */
static const char *field_at(const char *payload, uint16_t len, uint8_t n) {
    const char *p = payload;
    const char *end = payload + len;
    while (n > 0 && p < end) {
        if (*p == ',') n--;
        p++;
    }
    return (p < end) ? p : NULL;
}

static uint8_t field_len(const char *start, const char *payload_end) {
    const char *p = start;
    while (p < payload_end && *p != ',' && *p != '*') p++;
    return (uint8_t)(p - start);
}

static void field_copy(char *dst, const char *src, uint8_t len) {
    if (len > 31) len = 31;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ---- 公共 API ---- */

void Parser_Init(UartParser_t *p) {
    memset(p, 0, sizeof(*p));
}

bool Parser_Feed(UartParser_t *p, uint8_t byte, ParsedPacket_t *out) {
    if (!p->in_frame) {
        if (byte == '$') {
            p->in_frame = true;
            p->idx = 0;
        }
        return false;
    }

    /* 帧结束符 \n(前面应有 \r，但只判断 \n 更健壮) */
    if (byte == '\n') {
        p->in_frame = false;
        if (p->idx == 0) return false;

        /* 找校验和 '*' */
        uint8_t *star = NULL;
        for (uint16_t i = 0; i < p->idx; i++) {
            if (p->buf[i] == '*') { star = &p->buf[i]; break; }
        }
        if (star == NULL) return false;

        uint16_t payload_len = (uint16_t)(star - p->buf);
        if (payload_len == 0) return false;

        /* 校验 XOR */
        uint8_t expected = calc_xor(p->buf, payload_len);
        char cs_str[3] = {0};
        uint16_t cs_len = p->idx - payload_len - 1;
        if (cs_len > 2) cs_len = 2;
        if (cs_len >= 1) cs_str[0] = (char)star[1];
        if (cs_len >= 2) cs_str[1] = (char)star[2];
        uint8_t received = (uint8_t)strtol(cs_str, NULL, 16);
        if (expected != received) return false;

        /* 解析命令 */
        const char *cmd_start = (const char *)p->buf;
        uint16_t cmd_len = 0;
        const char *first_comma = memchr(cmd_start, ',', payload_len);
        if (first_comma) {
            cmd_len = (uint16_t)(first_comma - cmd_start);
        } else {
            cmd_len = payload_len;
        }

        memset(out, 0, sizeof(*out));
        out->type = find_cmd_type(cmd_start, cmd_len);

        switch (out->type) {
        case PKT_BOARD: {
            const char *fw = field_at(cmd_start, payload_len, 1);
            const char *fh = field_at(cmd_start, payload_len, 2);
            if (fw && fh) {
                char tmp[32];
                field_copy(tmp, fw, field_len(fw, p->buf + payload_len));
                out->board_w = (float)strtof(tmp, NULL);
                field_copy(tmp, fh, field_len(fh, p->buf + payload_len));
                out->board_h = (float)strtof(tmp, NULL);
            }
            break;
        }
        case PKT_COMP: {
            char tmp[32];
            const char *f;
            f = field_at(cmd_start, payload_len, 1);
            if (f) { field_copy(tmp, f, field_len(f, p->buf + payload_len)); out->comp_id = (uint16_t)strtol(tmp, NULL, 10); }
            f = field_at(cmd_start, payload_len, 2);
            if (f) { field_copy(tmp, f, field_len(f, p->buf + payload_len)); out->x = (float)strtof(tmp, NULL); }
            f = field_at(cmd_start, payload_len, 3);
            if (f) { field_copy(tmp, f, field_len(f, p->buf + payload_len)); out->y = (float)strtof(tmp, NULL); }
            f = field_at(cmd_start, payload_len, 4);
            if (f) { field_copy(tmp, f, field_len(f, p->buf + payload_len)); out->angle = (float)strtof(tmp, NULL); }
            f = field_at(cmd_start, payload_len, 5);
            if (f) { field_copy(tmp, f, field_len(f, p->buf + payload_len)); out->feeder_id = (uint8_t)strtol(tmp, NULL, 10); }
            break;
        }
        case PKT_ACK:
        case PKT_CAM_OK: {
            const char *f = field_at(cmd_start, payload_len, 1);
            if (f) {
                char tmp[32];
                field_copy(tmp, f, field_len(f, p->buf + payload_len));
                out->comp_id = (uint16_t)strtol(tmp, NULL, 10);
            }
            /* CAM_OK 还带 x,y,angle */
            if (out->type == PKT_CAM_OK) {
                f = field_at(cmd_start, payload_len, 2);
                if (f) { char tmp[32]; field_copy(tmp, f, field_len(f, p->buf + payload_len)); out->x = (float)strtof(tmp, NULL); }
                f = field_at(cmd_start, payload_len, 3);
                if (f) { char tmp[32]; field_copy(tmp, f, field_len(f, p->buf + payload_len)); out->y = (float)strtof(tmp, NULL); }
                f = field_at(cmd_start, payload_len, 4);
                if (f) { char tmp[32]; field_copy(tmp, f, field_len(f, p->buf + payload_len)); out->angle = (float)strtof(tmp, NULL); }
            }
            break;
        }
        case PKT_ERR: {
            const char *f = field_at(cmd_start, payload_len, 1);
            if (f) {
                char tmp[32];
                field_copy(tmp, f, field_len(f, p->buf + payload_len));
                out->comp_id = (uint16_t)strtol(tmp, NULL, 10);
            }
            f = field_at(cmd_start, payload_len, 2);
            if (f) {
                char tmp[32];
                field_copy(tmp, f, field_len(f, p->buf + payload_len));
                out->err_code = (uint8_t)strtol(tmp, NULL, 10);
            }
            break;
        }
        case PKT_CAM_ERR: {
            const char *f = field_at(cmd_start, payload_len, 1);
            if (f) {
                char tmp[32];
                field_copy(tmp, f, field_len(f, p->buf + payload_len));
                out->comp_id = (uint16_t)strtol(tmp, NULL, 10);
            }
            break;
        }
        default:
            break;
        }
        return true;
    }

    /* 普通字节:存入缓冲区 */
    if (p->idx < PARSER_FRAME_MAX) {
        p->buf[p->idx++] = byte;
    } else {
        /* 溢出，丢弃整帧 */
        p->in_frame = false;
        p->idx = 0;
    }
    return false;
}

uint16_t Parser_BuildFrame(PktType_t type, uint16_t comp_id,
                           float x, float y, float angle,
                           uint8_t feeder_id, uint8_t err_code,
                           float bw, float bh,
                           char *buf, uint16_t buf_size) {
    int len = 0;
    switch (type) {
    case PKT_READY:
        len = snprintf(buf, buf_size, "$READY");
        break;
    case PKT_ACK:
        len = snprintf(buf, buf_size, "$ACK,%u", comp_id);
        break;
    case PKT_ERR:
        len = snprintf(buf, buf_size, "$ERR,%u,%u", comp_id, err_code);
        break;
    case PKT_DONE:
        len = snprintf(buf, buf_size, "$DONE");
        break;
    default:
        return 0;
    }
    if (len <= 0 || (uint16_t)len + 4 > buf_size) return 0;

    /* 追加校验和 */
    uint8_t cs = calc_xor((uint8_t *)(buf + 1), (uint16_t)(len - 1));
    len += snprintf(buf + len, buf_size - len, "*%02X", cs);

    /* 追加 \r\n */
    if ((uint16_t)len + 2 <= buf_size) {
        buf[len++] = '\r';
        buf[len++] = '\n';
    }
    return (uint16_t)len;
}