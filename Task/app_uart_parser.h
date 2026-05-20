#ifndef __APP_UART_PARSER_H
#define __APP_UART_PARSER_H

#include <stdint.h>
#include <stdbool.h>

/* 行缓冲区最大长度 */
#define LINE_BUF_MAX    512

/* 上位机命令类型 */
typedef enum {
    HCMD_NONE = 0,
    /* 调试单步移动 */
    HCMD_MOVE_UP,       HCMD_MOVE_DOWN,
    HCMD_MOVE_LEFT,     HCMD_MOVE_RIGHT,
    /* 调试连续移动 */
    HCMD_MOVE_UP_START,    HCMD_MOVE_DOWN_START,
    HCMD_MOVE_LEFT_START,  HCMD_MOVE_RIGHT_START,
    HCMD_MOVE_STOP,
    /* 坐标系 */
    HCMD_SET_ORIGIN,
    HCMD_MOVE_TO,       // 运动至指定坐标
    /* 退出调试 */
    HCMD_EXIT_DEBUG,
    /* 文件下载 - 原始行 */
    HCMD_RAW_LINE,
    /* 未知命令 */
    HCMD_UNKNOWN
} HostCmd_t;

/* 解析后的上位机命令 */
typedef struct {
    HostCmd_t cmd;
    float param;            /* 步长(mm)或速度(mm/s) */
    float param2;           // Y 坐标(mm) (MOVE_TO 使用)
    char  raw[LINE_BUF_MAX]; /* 原始行内容(文件下载时使用) */
    uint16_t raw_len;
} HostParsed_t;

/* 行解析器状态机 */
typedef struct {
    char    buf[LINE_BUF_MAX];
    uint16_t idx;
    bool    complete;       /* 已收到完整一行 */
} LineParser_t;

/* ---- API ---- */
void LineParser_Init(LineParser_t *p);

/* 喂入一个字节，返回 true 表示收到完整一行(结果写入 out) */
bool LineParser_Feed(LineParser_t *p, uint8_t byte, HostParsed_t *out);

/* 构建单片机→上位机消息(返回长度，不含结尾 \0) */
uint16_t LineParser_BuildMsg(const char *msg, char *buf, uint16_t buf_size);

#endif
