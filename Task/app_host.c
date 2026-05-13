#include "app_host.h"
#include "app_uart_parser.h"
#include "app_motion.h"
#include "driver_uart.h"
#include "app_test.h"
#include <string.h>
#include <stdio.h>

/* 任务内全局状态 */
static HostState_t g_state = HOST_IDLE;
static Component_t g_components[MAX_COMPONENTS];
static uint16_t g_comp_count = 0;
static uint16_t g_comp_index = 0;

/* 协议解析器实例(每个 UART 通道独立) */
static UartParser_t host_parser;

/* 队列句柄 */
osMessageQueueId_t host_pkt_queue = NULL;

/* ---- 回调: driver_uart 在 HAL_UARTEx_RxEventCallback 中调用 ---- */
void Host_UartRecvCallback(uint8_t *data, int len) {
    if (host_pkt_queue == NULL) return;

    /* 将收到的字节逐一喂给解析器 */
    for (int i = 0; i < len; i++) {
        ParsedPacket_t pkt;
        if (Parser_Feed(&host_parser, data[i], &pkt)) {
            /* 解析出完整一帧，放入队列 */
            osMessageQueuePut(host_pkt_queue, &pkt, 0, 0);
        }
    }
}

/* ---- 发送帧到上位机 ---- */
static void host_send_frame(PktType_t type, uint16_t comp_id,
                             uint8_t err_code) {
    char buf[128];
    uint16_t len = Parser_BuildFrame(type, comp_id, 0, 0, 0, 0, err_code, 0, 0,
                                      buf, sizeof(buf));
    if (len > 0) {
        UART_SendData(UART_CH1, (uint8_t *)buf, len);
    }
}

/* ---- 上位机通信任务 ---- */
void Host_Task(void *argument) {
    (void)argument;

    /* 初始化队列和解析器 */
    host_pkt_queue = osMessageQueueNew(32, sizeof(ParsedPacket_t), NULL);
    Parser_Init(&host_parser);
    g_state = HOST_IDLE;
    g_comp_count = 0;
    g_comp_index = 0;
    memset(g_components, 0, sizeof(g_components));

    /* 启动时通知上位机就绪 */
    osDelay(500);
    host_send_frame(PKT_READY, 0, 0);
    PrintDebug("[HOST] Ready sent.\r\n");

    ParsedPacket_t pkt;

    for (;;) {
        if (osMessageQueueGet(host_pkt_queue, &pkt, NULL, osWaitForever) != osOK) {
            continue;
        }

        switch (g_state) {

        case HOST_IDLE:
            if (pkt.type == PKT_BOARD) {
                PrintDebug("[HOST] Board: %.1f x %.1f mm\r\n",
                           (double)pkt.board_w, (double)pkt.board_h);
                g_comp_count = 0;
                g_comp_index = 0;
                g_state = HOST_RECEIVING;
            }
            break;

        case HOST_RECEIVING:
            if (pkt.type == PKT_COMP) {
                if (g_comp_count < MAX_COMPONENTS) {
                    Component_t *c = &g_components[g_comp_count];
                    c->id = pkt.comp_id;
                    c->target_x = pkt.x;
                    c->target_y = pkt.y;
                    c->target_angle = pkt.angle;
                    c->feeder_id = pkt.feeder_id;
                    c->placed = false;
                    g_comp_count++;
                    PrintDebug("[HOST] Comp %u: (%.1f,%.1f) ang=%.1f feed=%u\r\n",
                               c->id, (double)c->target_x, (double)c->target_y,
                               (double)c->target_angle, c->feeder_id);
                }
            } else if (pkt.type == PKT_END) {
                PrintDebug("[HOST] End of board. Total %u components.\r\n",
                           g_comp_count);
                if (g_comp_count > 0) {
                    g_state = HOST_PROCESSING;
                    g_comp_index = 0;
                    /* 触发第一个元件的视觉查找 */
                    Component_t *c = &g_components[0];
                    char buf[32];
                    int len = snprintf(buf, sizeof(buf), "FIND,%u\r\n", c->id);
                    if (len > 0) {
                        UART_SendData(UART_CH2, (uint8_t *)buf, (uint16_t)len);
                        PrintDebug("[HOST] -> Cam: %s", buf);
                    }
                } else {
                    /* 空板，直接返回完成 */
                    host_send_frame(PKT_DONE, 0, 0);
                    g_state = HOST_IDLE;
                    osDelay(200);
                    host_send_frame(PKT_READY, 0, 0);
                }
            }
            break;

        case HOST_PROCESSING:
            if (pkt.type == PKT_CAM_OK) {
                /* 摄像头找到了元件 */
                Component_t *c = &g_components[g_comp_index];
                if (pkt.comp_id != c->id) {
                    PrintDebug("[HOST] Cam id mismatch: got %u, expected %u\r\n",
                               pkt.comp_id, c->id);
                    continue;
                }
                PrintDebug("[HOST] Cam found comp %u: offset(%.1f,%.1f) ang=%.1f\r\n",
                           c->id, (double)pkt.x, (double)pkt.y, (double)pkt.angle);

                /* 组装运动指令：拾取 */
                MotionCmd_t cmd;
                memset(&cmd, 0, sizeof(cmd));
                cmd.cmd_type = MOTION_CMD_PICK;
                cmd.target_x = (int32_t)((c->target_x + pkt.x) * 100.0f);
                cmd.target_y = (int32_t)((c->target_y + pkt.y) * 100.0f);
                cmd.target_r = (int32_t)(c->target_angle + pkt.angle) * 100;
                cmd.speed = 300;
                cmd.acc = 10;
                osMessageQueuePut(motion_cmd_queue, &cmd, 0, 0);

                c->placed = true;
                host_send_frame(PKT_ACK, c->id, 0);

                /* 处理下一个元件 */
                g_comp_index++;
                if (g_comp_index >= g_comp_count) {
                    /* 整板完成 */
                    host_send_frame(PKT_DONE, 0, 0);
                    PrintDebug("[HOST] Board complete!\r\n");
                    g_comp_count = 0;
                    g_state = HOST_IDLE;
                    osDelay(200);
                    host_send_frame(PKT_READY, 0, 0);
                } else {
                    /* 查找下一个元件 */
                    Component_t *next = &g_components[g_comp_index];
                    char buf[32];
                    int len = snprintf(buf, sizeof(buf), "FIND,%u\r\n", next->id);
                    if (len > 0) {
                        UART_SendData(UART_CH2, (uint8_t *)buf, (uint16_t)len);
                    }
                }
            } else if (pkt.type == PKT_CAM_ERR) {
                /* 摄像头未找到元件 */
                Component_t *c = &g_components[g_comp_index];
                PrintDebug("[HOST] Cam MISS comp %u\r\n", c->id);
                host_send_frame(PKT_ERR, c->id, 1);

                g_comp_index++;
                if (g_comp_index >= g_comp_count) {
                    host_send_frame(PKT_DONE, 0, 0);
                    g_comp_count = 0;
                    g_state = HOST_IDLE;
                    osDelay(200);
                    host_send_frame(PKT_READY, 0, 0);
                } else {
                    Component_t *next = &g_components[g_comp_index];
                    char buf[32];
                    int len = snprintf(buf, sizeof(buf), "FIND,%u\r\n", next->id);
                    if (len > 0) {
                        UART_SendData(UART_CH2, (uint8_t *)buf, (uint16_t)len);
                    }
                }
            }
            break;

        case HOST_DONE:
        default:
            break;
        }
    }
}