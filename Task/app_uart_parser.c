#include "app_uart_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- 内部辅助 ---- */

/* 匹配命令关键字，返回命令类型和参数 */
static HostCmd_t parse_cmd(const char *line, uint16_t len, float *param) {
    *param = 0.0f;
    if (len == 0) return HCMD_NONE;

    /* 找到第一个空格(分隔命令和参数) */
    const char *space = memchr(line, ' ', len);
    uint16_t cmd_len = space ? (uint16_t)(space - line) : len;

    /* 命令匹配表 */
    #define MATCH(s) (cmd_len == sizeof(s)-1 && memcmp(line, s, cmd_len) == 0)

    if (MATCH("MOVE_UP")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_MOVE_UP;
    }
    if (MATCH("MOVE_DOWN")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_MOVE_DOWN;
    }
    if (MATCH("MOVE_LEFT")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_MOVE_LEFT;
    }
    if (MATCH("MOVE_RIGHT")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_MOVE_RIGHT;
    }
    if (MATCH("MOVE_UP_START")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_MOVE_UP_START;
    }
    if (MATCH("MOVE_DOWN_START")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_MOVE_DOWN_START;
    }
    if (MATCH("MOVE_LEFT_START")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_MOVE_LEFT_START;
    }
    if (MATCH("MOVE_RIGHT_START")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_MOVE_RIGHT_START;
    }
    if (MATCH("MOVE_STOP")) {
        return HCMD_MOVE_STOP;
    }
    if (MATCH("SET_ORIGIN")) {
        return HCMD_SET_ORIGIN;
    }
    if (MATCH("EXIT_DEBUG_MODE")) {
        return HCMD_EXIT_DEBUG;
    }

    #undef MATCH
    return HCMD_UNKNOWN;
}

/* ---- 公共 API ---- */

void LineParser_Init(LineParser_t *p) {
    memset(p, 0, sizeof(*p));
}

bool LineParser_Feed(LineParser_t *p, uint8_t byte, HostParsed_t *out) {
    /* 行结束符 \n */
    if (byte == '\n') {
        if (p->idx == 0) return false;

        /* 去掉末尾可能的 \r */
        if (p->idx > 0 && p->buf[p->idx - 1] == '\r') {
            p->idx--;
        }
        p->buf[p->idx] = '\0';

        /* 解析命令 */
        memset(out, 0, sizeof(*out));
        out->cmd = parse_cmd(p->buf, p->idx, &out->param);

        /* 非命令 → 作为原始行(文件下载数据) */
        if (out->cmd == HCMD_UNKNOWN || out->cmd == HCMD_NONE) {
            out->cmd = HCMD_RAW_LINE;
            out->raw_len = (p->idx < LINE_BUF_MAX) ? p->idx : (LINE_BUF_MAX - 1);
            memcpy(out->raw, p->buf, out->raw_len);
            out->raw[out->raw_len] = '\0';
        }

        p->idx = 0;
        return true;
    }

    /* 忽略 \r */
    if (byte == '\r') return false;

    /* 收集字节 */
    if (p->idx < LINE_BUF_MAX - 1) {
        p->buf[p->idx++] = (char)byte;
    }
    return false;
}

uint16_t LineParser_BuildMsg(const char *msg, char *buf, uint16_t buf_size) {
    uint16_t len = (uint16_t)strlen(msg);
    if (len + 1 > buf_size) return 0;
    memcpy(buf, msg, len);
    buf[len] = '\n';
    return len + 1;
}