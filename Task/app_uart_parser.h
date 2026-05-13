#ifndef __APP_UART_PARSER_H
#define __APP_UART_PARSER_H

#include <stdint.h>
#include <stdbool.h>

/* 协议帧最大长度 */
#define PARSER_FRAME_MAX    256

/* 命令类型枚举 */
typedef enum {
    PKT_NONE = 0,
    /* 上位机 -> G4 */
    PKT_BOARD,          /* BOARD,w,h           板子尺寸 */
    PKT_COMP,           /* COMP,id,x,y,ang,feeder  元件坐标 */
    PKT_END,            /* END                传输结束 */
    /* G4 -> 上位机 */
    PKT_READY,          /* READY              G4就绪 */
    PKT_ACK,            /* ACK,id             贴装完成 */
    PKT_ERR,            /* ERR,id,code        错误报告 */
    PKT_DONE,           /* DONE               整板完成 */
    /* 摄像头 -> G4 */
    PKT_CAM_OK,         /* OK,id,x,y,angle    找到元件 */
    PKT_CAM_ERR,        /* ERR,id             未找到 */
    PKT_UNKNOWN         /* 未知命令 */
} PktType_t;

/* 解析后的数据包 */
typedef struct {
    PktType_t type;
    uint16_t comp_id;       /* 元件ID */
    float x;                /* X坐标(mm) */
    float y;                /* Y坐标(mm) */
    float angle;            /* 角度(deg) */
    uint8_t feeder_id;      /* 供料器编号 */
    float board_w;          /* 板宽 */
    float board_h;          /* 板高 */
    uint8_t err_code;       /* 错误码 */
} ParsedPacket_t;

/* 协议解析器状态机 */
typedef struct {
    uint8_t buf[PARSER_FRAME_MAX];
    uint16_t idx;
    bool in_frame;          /* 已收到 '$' 起始符 */
} UartParser_t;

/* ---- API ---- */
void Parser_Init(UartParser_t *p);

/* 喂入一个字节，返回 true 表示解析出完整一帧(结果写入 out) */
bool Parser_Feed(UartParser_t *p, uint8_t byte, ParsedPacket_t *out);

/* 将数据包格式化为发送帧(写入 buf，返回帧长度，不含结尾 '\0') */
uint16_t Parser_BuildFrame(PktType_t type, uint16_t comp_id,
                           float x, float y, float angle,
                           uint8_t feeder_id, uint8_t err_code,
                           float bw, float bh,
                           char *buf, uint16_t buf_size);

#endif