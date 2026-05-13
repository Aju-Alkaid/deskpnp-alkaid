#include "app_vision.h"
#include "app_uart_parser.h"
#include "app_host.h"
#include "app_test.h"
#include <string.h>

/* 摄像头协议解析器 */
static UartParser_t cam_parser;

void Vision_Init(void) {
    Parser_Init(&cam_parser);
    PrintDebug("[VISION] Init done.\r\n");
}

/*
 * 回调: driver_uart 收到 UART2 数据时调用
 * 解析出完整帧后，放入 host_pkt_queue 供 Host_Task 消费
 */
void CamUart_RecvCallback(uint8_t *data, int len) {
    if (host_pkt_queue == NULL) return;

    for (int i = 0; i < len; i++) {
        ParsedPacket_t pkt;
        if (Parser_Feed(&cam_parser, data[i], &pkt)) {
            if (pkt.type == PKT_CAM_OK || pkt.type == PKT_CAM_ERR) {
                osMessageQueuePut(host_pkt_queue, &pkt, 0, 0);
                PrintDebug("[VISION] Rx: type=%d id=%u\r\n", pkt.type, pkt.comp_id);
            }
        }
    }
}