#include "app_uart_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- 内部辅助 ---- */

/* 匹配命令关键字，返回命令类型和参数 */
static HostCmd_t parse_cmd(const char *line, uint16_t len, float *param, float *param2) {
    *param = 0.0f;
    *param2 = 0.0f;
    if (len == 0) return HCMD_NONE;

    /* 找到第一个空格(分隔命令和参数) */
    const char *space = memchr(line, ' ', len);
    uint16_t cmd_len = space ? (uint16_t)(space - line) : len;

    /* 命令匹配表 */
    #define MATCH(s) (cmd_len == sizeof(s)-1 && memcmp(line, s, cmd_len) == 0)

    // MATCH 通过长度+内容双重校验，排序不影响正确性（如 MOVE_UP 不会误匹配 MOVE_UP_START）
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
    if (MATCH("MOVE_TO")) {
        if (space) {
            char *p = (char*)(space + 1);
            *param  = strtof(p, &p);
            *param2 = strtof(p, NULL);
        }
        return HCMD_MOVE_TO;
    }
    if (MATCH("SET_SERVO")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_SET_SERVO;
    }
    if (MATCH("SET_R_AXIS")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_SET_R_AXIS;
    }
    if (MATCH("PUMP_ON")) {
        return HCMD_PUMP_ON;
    }
    if (MATCH("PUMP_OFF")) {
        return HCMD_PUMP_OFF;
    }
    if (MATCH("HEAT_ON")) {
        return HCMD_HEAT_ON;
    }
    if (MATCH("HEAT_OFF")) {
        return HCMD_HEAT_OFF;
    }
    if (MATCH("SET_ORIGIN")) {
        return HCMD_SET_ORIGIN;
    }
    if (MATCH("EXIT_DEBUG_MODE")) {
        return HCMD_EXIT_DEBUG;
    }


    if (MATCH("SET_SCATTER_AREA")) {
        return HCMD_SET_SCATTER_AREA;
    }
    if (MATCH("SET_SCATTER_SIZE")) {
        if (space) *param = (float)strtof(space + 1, NULL);
        return HCMD_SET_SCATTER_SIZE;
    }
    if (MATCH("SET_HEATER_PLATFORM_MIN")) {
        return HCMD_SET_HEATER_PLATFORM_MIN;
    }
    if (MATCH("SET_HEATER_PLATFORM_MAX")) {
        return HCMD_SET_HEATER_PLATFORM_MAX;
    }
    if (MATCH("SET_BOTTOM_CAM")) {
        return HCMD_SET_BOTTOM_CAM;
    }
    if (MATCH("SET_Z_SAFE")) {
        return HCMD_SET_Z_SAFE;
    }
    if (MATCH("SET_Z_PICK")) {
        return HCMD_SET_Z_PICK;
    }
    if (MATCH("SET_Z_PLACE")) {
        return HCMD_SET_Z_PLACE;
    }
    if (MATCH("SET_R_ZERO")) {
        return HCMD_SET_R_ZERO;
    }
    if (MATCH("SET_CAM_OFFSET")) {
        return HCMD_SET_CAM_OFFSET;
    }
    if (MATCH("SAVE_CALIB")) {
        return HCMD_SAVE_CALIB;
    }
    if (MATCH("RESTORE_CALIB")) {
        return HCMD_RESTORE_CALIB;
    }
    if (MATCH("RESUME")) {
        return HCMD_RESUME;
    }
    if (MATCH("ABORT")) {
        return HCMD_ABORT;
    }
    if (MATCH("AUTO_HEAT")) {
        return HCMD_AUTO_HEAT;
    }
    if (MATCH("HOME")) {
        return HCMD_HOME;
    }
    if (MATCH("VALVE_ON")) {
        return HCMD_VALVE_ON;
    }
    if (MATCH("VALVE_OFF")) {
        return HCMD_VALVE_OFF;
    }
    #undef MATCH#undef MATCH
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
        out->cmd = parse_cmd(p->buf, p->idx, &out->param, &out->param2);

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
