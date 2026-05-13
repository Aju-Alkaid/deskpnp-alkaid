#include "app_host.h"
#include "app_motion.h"
#include "driver_uart.h"
#include "app_test.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- 任务内全局状态 ---- */
static HostState_t  g_state = HOST_IDLE;
static Component_t  g_components[MAX_COMPONENTS];
static uint16_t     g_comp_count = 0;
static uint16_t     g_comp_index = 0;

/* 下载超时检测 */
static uint32_t     g_last_line_tick = 0;
static bool         g_header_parsed = false;

/* CSV列索引(根据表头动态确定) */
static int8_t g_col_x    = -1;
static int8_t g_col_y    = -1;
static int8_t g_col_rot  = -1;
static int8_t g_col_smd  = -1;
static int8_t g_max_col  = 0;

/* Mark点对齐偏移 */
static int32_t g_align_dx = 0;
static int32_t g_align_dy = 0;

/* 调试模式当前坐标(编码器脉冲) */
static int32_t g_dbg_cur_x = 0;
static int32_t g_dbg_cur_y = 0;

/* 行解析器 */
static LineParser_t g_parser;

/* 队列句柄 */
osMessageQueueId_t host_pkt_queue = NULL;

/* ---- 内部辅助 ---- */

/* 发送字符串到上位机(自动加 \n) */
static void host_send(const char *msg) {
    char buf[128];
    uint16_t len = LineParser_BuildMsg(msg, buf, sizeof(buf));
    if (len > 0) {
        UART_SendData(UART_CH1, (uint8_t *)buf, len);
    }
}

/* 解析CSV表头行，找到各字段列号 */
static void parse_header(const char *line, uint16_t len) {
    g_col_x = -1; g_col_y = -1; g_col_rot = -1; g_col_smd = -1;
    g_max_col = 0;

    int8_t col = 0;
    const char *p = line;
    const char *end = line + len;

    while (p <= end) {
        const char *next = memchr(p, ',', (uint16_t)(end - p));
        uint16_t flen = next ? (uint16_t)(next - p) : (uint16_t)(end - p);

        /* 去掉引号 */
        if (flen >= 2 && *p == '"' && p[flen-1] == '"') {
            p++; flen -= 2;
        }

        /* 匹配常见表头名称 */
        if ((flen >= 1 && (p[0] == 'X' || p[0] == 'x')) ||
            (flen >= 4 && (memcmp(p, "Mid X", 5) == 0 || memcmp(p, "PosX", 4) == 0 ||
                           memcmp(p, "Center X", 8) == 0))) {
            g_col_x = col;
        } else if ((flen >= 1 && (p[0] == 'Y' || p[0] == 'y')) ||
                   (flen >= 4 && (memcmp(p, "Mid Y", 5) == 0 || memcmp(p, "PosY", 4) == 0 ||
                                  memcmp(p, "Center Y", 8) == 0))) {
            g_col_y = col;
        } else if (flen >= 3 && (memcmp(p, "Rot", 3) == 0 || memcmp(p, "Rotation", 8) == 0 ||
                                 memcmp(p, "Angle", 5) == 0)) {
            g_col_rot = col;
        } else if (flen >= 3 && (memcmp(p, "SMD", 3) == 0 || memcmp(p, "smd", 3) == 0 ||
                                 memcmp(p, "Type", 4) == 0)) {
            g_col_smd = col;
        }

        col++;
        if (next == NULL) break;
        p = next + 1;
    }
    g_max_col = col;

    PrintDebug("[HOST] Header parsed: Xcol=%d Ycol=%d Rcol=%d Scol=%d\r\n",
               g_col_x, g_col_y, g_col_rot, g_col_smd);
}

/* 从CSV行中提取第n个字段值 */
static bool get_csv_field(const char *line, uint16_t len, int8_t n,
                          char *out, uint16_t out_max) {
    if (n < 0) return false;
    const char *p = line;
    const char *end = line + len;
    int8_t col = 0;

    while (col < n && p < end) {
        p = memchr(p, ',', (uint16_t)(end - p));
        if (p == NULL) return false;
        p++;
        col++;
    }
    if (p >= end) return false;

    const char *next = memchr(p, ',', (uint16_t)(end - p));
    uint16_t flen = next ? (uint16_t)(next - p) : (uint16_t)(end - p);
    if (flen >= out_max) flen = out_max - 1;
    memcpy(out, p, flen);
    out[flen] = '\0';
    return true;
}

/* 解析一行CSV数据，放入元件数组 */
static bool parse_csv_line(const char *line, uint16_t len) {
    if (g_comp_count >= MAX_COMPONENTS) return false;

    /* SMD过滤 */
    if (g_col_smd >= 0) {
        char smd_str[8];
        if (get_csv_field(line, len, g_col_smd, smd_str, sizeof(smd_str))) {
            if (smd_str[0] == '0' || smd_str[0] == 'N' || smd_str[0] == 'n' ||
                smd_str[0] == 'F' || smd_str[0] == 'f') {
                return false;
            }
        }
    }

    /* 如果所有关键列都未识别，使用默认列映射(col0=id, col3=x, col4=y, col5=rot) */
    if (g_col_x < 0 && g_col_y < 0) {
        g_col_x = 3; g_col_y = 4; g_col_rot = 5;
        PrintDebug("[HOST] Using default column mapping.\r\n");
    }

    Component_t *c = &g_components[g_comp_count];
    memset(c, 0, sizeof(*c));
    c->id = g_comp_count + 1;
    c->feeder_id = 1;

    char tmp[32];
    if (get_csv_field(line, len, g_col_x, tmp, sizeof(tmp))) {
        c->target_x = (float)strtof(tmp, NULL);
    }
    if (get_csv_field(line, len, g_col_y, tmp, sizeof(tmp))) {
        c->target_y = (float)strtof(tmp, NULL);
    }
    if (g_col_rot >= 0) {
        if (get_csv_field(line, len, g_col_rot, tmp, sizeof(tmp))) {
            c->target_angle = (float)strtof(tmp, NULL);
        }
    }

    g_comp_count++;
    return true;
}

/* 下载完成，开始贴装流程 */
static void download_done(void) {
    PrintDebug("[HOST] Download done. %u components loaded.\r\n", g_comp_count);
    g_header_parsed = false;

    if (g_comp_count > 0) {
        g_state = HOST_ALIGN;
        Vision_SendCmd(CAM_CMD_PROC2);
        PrintDebug("[HOST] Starting Mark alignment...\r\n");
    } else {
        host_send("DOWNLOAD_READY");
        g_state = HOST_IDLE;
    }
}

/* 开始查找下一个元件 */
static void start_next_comp(void) {
    if (g_comp_index >= g_comp_count) {
        PrintDebug("[HOST] All %u components placed!\r\n", g_comp_count);
        g_comp_count = 0;
        g_state = HOST_IDLE;
        osDelay(200);
        host_send("DOWNLOAD_READY");
    } else {
        g_state = HOST_FIND_COMP;
        Vision_SendCmd(CAM_CMD_PROC1);
        PrintDebug("[HOST] Looking for comp %u...\r\n",
                   g_components[g_comp_index].id);
    }
}

/* ---- 回调: driver_uart 在 HAL_UARTEx_RxEventCallback 中调用 ---- */
void Host_UartRecvCallback(uint8_t *data, int len) {
    if (host_pkt_queue == NULL) return;

    for (int i = 0; i < len; i++) {
        HostParsed_t parsed;
        if (LineParser_Feed(&g_parser, data[i], &parsed)) {
            HostMsg_t msg;
            msg.type = MSG_HOST_CMD;
            msg.data.host_cmd = parsed;
            osMessageQueuePut(host_pkt_queue, &msg, 0, 0);
        }
    }
}

/* ---- 上位机通信任务 ---- */
void Host_Task(void *argument) {
    (void)argument;

    host_pkt_queue = osMessageQueueNew(64, sizeof(HostMsg_t), NULL);
    LineParser_Init(&g_parser);
    g_state = HOST_IDLE;
    g_comp_count = 0;
    g_comp_index = 0;
    g_header_parsed = false;
    g_align_dx = 0;
    g_align_dy = 0;
    memset(g_components, 0, sizeof(g_components));

    osDelay(500);
    host_send("DOWNLOAD_READY");
    PrintDebug("[HOST] DOWNLOAD_READY sent.\r\n");

    HostMsg_t msg;

    for (;;) {
        /* 下载超时检测(仅 DOWNLOADING 状态) */
        if (g_state == HOST_DOWNLOADING && g_comp_count > 0) {
            uint32_t elapsed = osKernelGetTickCount() - g_last_line_tick;
            if (elapsed >= DOWNLOAD_TIMEOUT_MS) {
                download_done();
                continue;
            }
        }

        /* 带超时的队列等待 */
        if (osMessageQueueGet(host_pkt_queue, &msg, NULL, 50) != osOK) {
            continue;
        }

        /* ===== 处理上位机命令 ===== */
        if (msg.type == MSG_HOST_CMD) {
            HostParsed_t *cmd = &msg.data.host_cmd;

            /* 调试模式下的命令处理 */
            if (g_state == HOST_DEBUG) {
                switch (cmd->cmd) {
                case HCMD_EXIT_DEBUG:
                    PrintDebug("[HOST] Exit debug mode.\r\n");
                    g_state = HOST_IDLE;
                    g_dbg_cur_x = 0;
                    g_dbg_cur_y = 0;
                    osDelay(200);
                    host_send("DOWNLOAD_READY");
                    break;

                case HCMD_MOVE_UP: case HCMD_MOVE_DOWN:
                case HCMD_MOVE_LEFT: case HCMD_MOVE_RIGHT: {
                    float step = cmd->param;
                    if (step <= 0.0f) step = 1.0f;
                    /* 转为脉冲(假设 1mm = 1000 脉冲，需根据实际调整) */
                    int32_t dx = 0, dy = 0;
                    if (cmd->cmd == HCMD_MOVE_UP)    dy = (int32_t)(step * 1000);
                    if (cmd->cmd == HCMD_MOVE_DOWN)  dy = -(int32_t)(step * 1000);
                    if (cmd->cmd == HCMD_MOVE_LEFT)  dx = -(int32_t)(step * 1000);
                    if (cmd->cmd == HCMD_MOVE_RIGHT) dx = (int32_t)(step * 1000);

                    g_dbg_cur_x += dx;
                    g_dbg_cur_y += dy;

                    MotionCmd_t mcmd;
                    memset(&mcmd, 0, sizeof(mcmd));
                    mcmd.cmd_type = MOTION_CMD_MOVE_TO;
                    mcmd.target_x = g_dbg_cur_x;
                    mcmd.target_y = g_dbg_cur_y;
                    mcmd.speed = 200;
                    mcmd.acc = 10;
                    osMessageQueuePut(motion_cmd_queue, &mcmd, 0, 0);
                    PrintDebug("[HOST] Debug move: step=%.1f pos=(%ld,%ld)\r\n",
                               (double)step, (long)g_dbg_cur_x, (long)g_dbg_cur_y);
                    break;
                }

                case HCMD_MOVE_UP_START: case HCMD_MOVE_DOWN_START:
                case HCMD_MOVE_LEFT_START: case HCMD_MOVE_RIGHT_START:
                    PrintDebug("[HOST] Continuous move start, speed=%.1f\r\n",
                               (double)cmd->param);
                    break;

                case HCMD_MOVE_STOP:
                    PrintDebug("[HOST] Continuous move stop.\r\n");
                    break;

                case HCMD_SET_ORIGIN:
                    g_dbg_cur_x = 0;
                    g_dbg_cur_y = 0;
                    PrintDebug("[HOST] Origin set to (0,0).\r\n");
                    break;

                default:
                    break;
                }
                continue;
            }

            /* 非调试模式下的处理 */
            switch (cmd->cmd) {

            case HCMD_RAW_LINE:
                /* 文件下载中的数据行 */
                if (g_state == HOST_IDLE) {
                    g_state = HOST_DOWNLOADING;
                    g_comp_count = 0;
                    g_header_parsed = false;
                    PrintDebug("[HOST] Download started.\r\n");
                }
                if (g_state == HOST_DOWNLOADING) {
                    g_last_line_tick = osKernelGetTickCount();

                    if (!g_header_parsed) {
                        parse_header(cmd->raw, cmd->raw_len);
                        g_header_parsed = true;
                    } else {
                        parse_csv_line(cmd->raw, cmd->raw_len);
                    }
                }
                break;

            case HCMD_EXIT_DEBUG:
                PrintDebug("[HOST] EXIT_DEBUG from host (ignored in non-debug).\r\n");
                break;

            default:
                PrintDebug("[HOST] Unknown cmd in state %d\r\n", g_state);
                break;
            }
        }

        /* ===== 处理摄像头数据 ===== */
        else if (msg.type == MSG_CAM_DATA) {
            CamData_t *cam = &msg.data.cam_data;

            switch (g_state) {

            case HOST_ALIGN:
                if (cam->result == CAM_PROC2_OK) {
                    PrintDebug("[HOST] Mark: (%ld,%ld) (%ld,%ld)\r\n",
                               (long)cam->mark1_x, (long)cam->mark1_y,
                               (long)cam->mark2_x, (long)cam->mark2_y);
                    g_align_dx = cam->mark1_x;
                    g_align_dy = cam->mark1_y;
                    g_comp_index = 0;
                    start_next_comp();
                } else {
                    PrintDebug("[HOST] Mark alignment failed!\r\n");
                    g_comp_count = 0;
                    g_state = HOST_IDLE;
                    host_send("DOWNLOAD_READY");
                }
                break;

            case HOST_FIND_COMP:
                if (cam->result == CAM_PROC1_OK) {
                    Component_t *c = &g_components[g_comp_index];
                    PrintDebug("[HOST] Comp %u found: offset(%ld,%ld)\r\n",
                               c->id, (long)cam->x_offset, (long)cam->y_offset);

                    MotionCmd_t mcmd;
                    memset(&mcmd, 0, sizeof(mcmd));
                    mcmd.cmd_type = MOTION_CMD_PICK;
                    mcmd.target_x = cam->x_offset + g_align_dx;
                    mcmd.target_y = cam->y_offset + g_align_dy;
                    mcmd.target_r = (int32_t)(c->target_angle * 100);
                    mcmd.speed = 300;
                    mcmd.acc = 10;
                    osMessageQueuePut(motion_cmd_queue, &mcmd, 0, 0);

                    osDelay(500);
                    g_state = HOST_OFFSET;
                    Vision_SendCmd(CAM_CMD_PROC3);
                } else {
                    PrintDebug("[HOST] Comp %u NOT FOUND!\r\n",
                               g_components[g_comp_index].id);
                    g_comp_index++;
                    start_next_comp();
                }
                break;

            case HOST_OFFSET:
                if (cam->result == CAM_PROC3_OK) {
                    Component_t *c = &g_components[g_comp_index];
                    PrintDebug("[HOST] Place offset: (%ld,%ld)\r\n",
                               (long)cam->x_offset, (long)cam->y_offset);

                    MotionCmd_t mcmd;
                    memset(&mcmd, 0, sizeof(mcmd));
                    mcmd.cmd_type = MOTION_CMD_PLACE;
                    mcmd.target_x = (int32_t)(c->target_x * 100) + cam->x_offset;
                    mcmd.target_y = (int32_t)(c->target_y * 100) + cam->y_offset;
                    mcmd.target_r = (int32_t)(c->target_angle * 100);
                    mcmd.speed = 200;
                    mcmd.acc = 10;
                    osMessageQueuePut(motion_cmd_queue, &mcmd, 0, 0);

                    c->placed = true;
                    g_comp_index++;
                    osDelay(200);
                    start_next_comp();
                } else {
                    PrintDebug("[HOST] Offset failed for comp %u\r\n",
                               g_components[g_comp_index].id);
                    g_comp_index++;
                    start_next_comp();
                }
                break;

            default:
                break;
            }
        }
    }
}