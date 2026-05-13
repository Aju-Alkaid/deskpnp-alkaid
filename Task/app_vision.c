#include "app_vision.h"
#include "app_host.h"
#include "app_test.h"
#include "driver_uart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- 摄像头帧协议常量 ---- */
#define CAM_FRAME_HEADER  0x7E
#define CAM_FRAME_FOOTER  0x7F
#define CAM_FIELD_MAX     32

/* ---- 帧解析状态机 ---- */
typedef enum {
    CAM_IDLE,
    CAM_IN_FIELD,
} CamParserState_t;

/* ---- 全局状态 ---- */
static CamParserState_t g_ps = CAM_IDLE;
static uint8_t  g_field_buf[CAM_FIELD_MAX];
static uint16_t g_field_idx = 0;
static bool     g_in_response = false;
static uint8_t  g_data_count = 0;

static CamData_t g_cam_data;
static CamCmd_t  g_pending_cmd = CAM_CMD_PROC1;

/* ---- 内部辅助 ---- */

static void reset_parser(void) {
    g_ps = CAM_IDLE;
    g_field_idx = 0;
    g_in_response = false;
    g_data_count = 0;
    memset(&g_cam_data, 0, sizeof(g_cam_data));
    g_cam_data.result = CAM_NONE;
}

static int32_t parse_field_int(void) {
    if (g_field_idx == 0) return 0;
    uint8_t safe_idx = (g_field_idx < CAM_FIELD_MAX) ? g_field_idx : (CAM_FIELD_MAX - 1);
    g_field_buf[safe_idx] = '\0';
    return (int32_t)strtol((const char *)g_field_buf, NULL, 10);
}

static bool field_is(const char *s) {
    uint8_t len = (uint8_t)strlen(s);
    if (g_field_idx != len) return false;
    return (memcmp(g_field_buf, s, len) == 0);
}

/* 将当前结果推入队列 */
static void push_result(void) {
    if (host_pkt_queue == NULL || g_cam_data.result == CAM_NONE) return;
    HostMsg_t msg;
    msg.type = MSG_CAM_DATA;
    msg.data.cam_data = g_cam_data;
    osMessageQueuePut(host_pkt_queue, &msg, 0, 0);
}

static void process_field(void) {
    if (field_is("begin")) {
        g_in_response = true;
        g_data_count = 0;
        memset(&g_cam_data, 0, sizeof(g_cam_data));
        g_cam_data.result = CAM_NONE;
        return;
    }

    if (field_is("end")) {
        if (g_in_response) {
            switch (g_pending_cmd) {
            case CAM_CMD_PROC1:
                g_cam_data.result = (g_data_count >= 3) ? CAM_PROC1_OK : CAM_PROC1_ERR;
                break;
            case CAM_CMD_PROC2:
                g_cam_data.result = (g_data_count >= 4) ? CAM_PROC2_OK : CAM_PROC2_ERR;
                break;
            case CAM_CMD_PROC3:
                g_cam_data.result = (g_data_count >= 2) ? CAM_PROC3_OK : CAM_PROC3_ERR;
                break;
            }
            push_result();
        }
        reset_parser();
        return;
    }

    if (field_is("err1")) {
        g_cam_data.result = CAM_PROC1_ERR;
        push_result();
        reset_parser();
        return;
    }
    if (field_is("err2")) {
        g_cam_data.result = CAM_PROC2_ERR;
        push_result();
        reset_parser();
        return;
    }
    if (field_is("err3")) {
        g_cam_data.result = CAM_PROC3_ERR;
        push_result();
        reset_parser();
        return;
    }

    /* 数据字段 */
    if (g_in_response) {
        int32_t val = parse_field_int();
        switch (g_pending_cmd) {
        case CAM_CMD_PROC1:
            if (g_data_count == 0)      g_cam_data.x_offset = val;
            else if (g_data_count == 1) g_cam_data.y_offset = val;
            else if (g_data_count == 2) g_cam_data.comp_info = val;
            break;
        case CAM_CMD_PROC2:
            if (g_data_count == 0)      g_cam_data.mark1_x = val;
            else if (g_data_count == 1) g_cam_data.mark1_y = val;
            else if (g_data_count == 2) g_cam_data.mark2_x = val;
            else if (g_data_count == 3) g_cam_data.mark2_y = val;
            break;
        case CAM_CMD_PROC3:
            if (g_data_count == 0)      g_cam_data.x_offset = val;
            else if (g_data_count == 1) g_cam_data.y_offset = val;
            break;
        }
        g_data_count++;
    }
}

/* ---- 公共 API ---- */

void Vision_Init(void) {
    reset_parser();
    PrintDebug("[VISION] Init done.\r\n");
}

void Vision_SendCmd(CamCmd_t cmd) {
    g_pending_cmd = cmd;
    reset_parser();

    const char *str = NULL;
    switch (cmd) {
    case CAM_CMD_PROC1: str = "process1"; break;
    case CAM_CMD_PROC2: str = "process2"; break;
    case CAM_CMD_PROC3: str = "process3"; break;
    }
    if (str) {
        UART_SendString(UART_CH2, str);
        PrintDebug("[VISION] -> Cam: %s\r\n", str);
    }
}

void CamUart_RecvCallback(uint8_t *data, int len) {
    for (int i = 0; i < len; i++) {
        uint8_t byte = data[i];

        switch (g_ps) {
        case CAM_IDLE:
            if (byte == CAM_FRAME_HEADER) {
                g_ps = CAM_IN_FIELD;
                g_field_idx = 0;
            }
            break;

        case CAM_IN_FIELD:
            if (byte == CAM_FRAME_FOOTER) {
                if (g_field_idx < CAM_FIELD_MAX) {
                    g_field_buf[g_field_idx] = '\0';
                }
                process_field();
                g_ps = CAM_IDLE;
            } else if (byte == CAM_FRAME_HEADER) {
                g_field_idx = 0;
            } else {
                if (g_field_idx < CAM_FIELD_MAX) {
                    g_field_buf[g_field_idx++] = byte;
                }
            }
            break;
        }
    }
}