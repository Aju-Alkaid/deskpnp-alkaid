#include "app_host.h"
#include "app_test.h"
#include "driver_uart.h"
#include "driver_can.h"
#include "driver_motor.h"
#include "driver_servo.h"
#include "driver_drv8803.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ================================================================
 *  常量
 * ================================================================ */
#define JOG_SPEED        300
#define JOG_ACC          25
#define JOG_MMS_TO_RPM  12.0f   /* mm/s → RPM: STEPS_PER_MM/16384*60 */
#define PNP_SPEED        300
#define PNP_ACC          25
#define PICK_DELAY_MS    300
#define PLACE_DELAY_MS   300

/* ================================================================
 *  任务内全局状态
 * ================================================================ */
static HostState_t  g_state = HOST_INIT;
static Component_t  g_components[MAX_COMPONENTS];
static uint16_t     g_comp_count = 0;
static uint16_t     g_comp_index = 0;

/* 下载超时检测 */
static uint32_t     g_last_line_tick = 0;
static bool         g_header_parsed = false;

/* CSV列索引 */
static int8_t g_col_x    = -1;
static int8_t g_col_y    = -1;
static int8_t g_col_rot  = -1;
static int8_t g_col_smd  = -1;
static int8_t g_max_col  = 0;

/* 当前坐标 (步数) */
static int32_t g_cur_x = 0;
static int32_t g_cur_y = 0;

/* JOG 状态 */
static bool g_jog_active = false;

/* 行解析器 */
static LineParser_t g_parser;

/* 队列句柄 (在 app_freertos.c 中创建，此处为 extern 引用) */


/* Mark 对齐累计偏移 */
static int32_t g_mark_offsets[P2_MARK_COUNT][2];  /* 3个Mark的(dx, dy) mm*10000 */

static int32_t g_mark_count_done = 0;

/* Mark 对齐平均偏移 (mm*10000)，PLACE 时应用到贴装坐标 */
static int32_t g_mark_avg_dx = 0;
static int32_t g_mark_avg_dy = 0;
/* ================================================================
 *  内部辅助
 * ================================================================ */

static void host_send(const char *msg) {
    char buf[128];
    uint16_t len = LineParser_BuildMsg(msg, buf, sizeof(buf));
    if (len > 0) {
        UART_SendData(UART_CH1, (uint8_t *)buf, len);
    }
}

/* ---- CSV 解析  ---- */

static void parse_header(const char *line, uint16_t len) {
    g_col_x = -1; g_col_y = -1; g_col_rot = -1; g_col_smd = -1;
    g_max_col = 0;

    int8_t col = 0;
    const char *p = line;
    const char *end = line + len;

    while (p <= end) {
        const char *next = memchr(p, ',', (uint16_t)(end - p));
        uint16_t flen = next ? (uint16_t)(next - p) : (uint16_t)(end - p);

        if (flen >= 2 && *p == '"' && p[flen-1] == '"') {
            p++; flen -= 2;
        }

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

    PrintDebug("[HOST] Header: X=%d Y=%d R=%d S=%d\r\n",
               g_col_x, g_col_y, g_col_rot, g_col_smd);
}

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

static bool parse_csv_line(const char *line, uint16_t len) {
    if (g_comp_count >= MAX_COMPONENTS) return false;

    if (g_col_smd >= 0) {
        char smd_str[8];
        if (get_csv_field(line, len, g_col_smd, smd_str, sizeof(smd_str))) {
            if (smd_str[0] == '0' || smd_str[0] == 'N' || smd_str[0] == 'n' ||
                smd_str[0] == 'F' || smd_str[0] == 'f') {
                return false;
            }
        }
    }

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

static void download_done(void) {
    PrintDebug("[HOST] Download done. %u components.\r\n", g_comp_count);
    g_header_parsed = false;

    if (g_comp_count > 0) {
        g_comp_index = 0;
        g_mark_count_done = 0;
        memset(g_mark_offsets, 0, sizeof(g_mark_offsets));
        g_state = HOST_MARK_ALIGN;
        Vision_Start(VCMD_P2);
        Vision_Go();
        PrintDebug("[HOST] Starting Mark alignment (P2)...\r\n");
    } else {
        host_send("DOWNLOAD_READY");
        g_state = HOST_DEBUG;
    }
}

/* ================================================================
 *  调试模式命令处理
 * ================================================================ */
static void handle_debug_cmd(HostParsed_t *cmd) {
    int32_t steps;

    switch (cmd->cmd) {
    case HCMD_MOVE_UP:
        steps = (int32_t)(cmd->param * STEPS_PER_MM);
        move_xy_relative(steps, 0, JOG_SPEED, JOG_ACC, &g_cur_x, &g_cur_y);
        g_jog_active = false;
        PrintDebug("[HOST] MOVE_UP %.1fmm→(%ld,%ld)\r\n",
                   cmd->param, g_cur_x, g_cur_y);
        break;

    case HCMD_MOVE_DOWN:
        steps = (int32_t)(-cmd->param * STEPS_PER_MM);
        move_xy_relative(steps, 0, JOG_SPEED, JOG_ACC, &g_cur_x, &g_cur_y);
        g_jog_active = false;
        PrintDebug("[HOST] MOVE_DOWN %.1fmm→(%ld,%ld)\r\n",
                   cmd->param, g_cur_x, g_cur_y);
        break;

    case HCMD_MOVE_LEFT:
        steps = (int32_t)(cmd->param * STEPS_PER_MM);
        move_xy_relative(0, steps, JOG_SPEED, JOG_ACC, &g_cur_x, &g_cur_y);
        g_jog_active = false;
        PrintDebug("[HOST] MOVE_LEFT %.1fmm→(%ld,%ld)\r\n",
                   cmd->param, g_cur_x, g_cur_y);
        break;

    case HCMD_MOVE_RIGHT:
        steps = (int32_t)(-cmd->param * STEPS_PER_MM);
        move_xy_relative(0, steps, JOG_SPEED, JOG_ACC, &g_cur_x, &g_cur_y);
        g_jog_active = false;
        PrintDebug("[HOST] MOVE_RIGHT %.1fmm→(%ld,%ld)\r\n",
                   cmd->param, g_cur_x, g_cur_y);
        break;

    case HCMD_MOVE_UP_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        motorSyncEnable(1);          /* 确保同步缓存模式 */
        osDelay(5);
        positionMode3Run(X1_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, JOG_MAX_STEPS);
        positionMode3Run(X2_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG UP %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_DOWN_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        motorSyncEnable(1);          /* 确保同步缓存模式 */
        osDelay(5);
        positionMode3Run(X1_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, -JOG_MAX_STEPS);
        positionMode3Run(X2_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, -JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG DOWN %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_LEFT_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        motorSyncEnable(1);          /* 确保同步缓存模式 */
        osDelay(5);
        positionMode3Run(Y_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG LEFT %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_RIGHT_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        motorSyncEnable(1);          /* 确保同步缓存模式 */
        osDelay(5);
        positionMode3Run(Y_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, -JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG RIGHT %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_STOP:
        disable_sync_stop();
        g_jog_active = false;
        PrintDebug("[HOST] STOP\r\n");
        break;

    case HCMD_MOVE_TO: {
        int32_t tx = (int32_t)(cmd->param  * STEPS_PER_MM);
        int32_t ty = (int32_t)(cmd->param2 * STEPS_PER_MM);
        if (g_jog_active) { disable_sync_stop(); g_jog_active = false; }
        int32_t dx = tx - g_cur_x;
        int32_t dy = ty - g_cur_y;
        move_xy_relative(dx, dy, JOG_SPEED, JOG_ACC, &g_cur_x, &g_cur_y);
        PrintDebug("[HOST] MOVE_TO (%.1f,%.1f)→(%ld,%ld)\r\n",
                   cmd->param, cmd->param2, g_cur_x, g_cur_y);
        break;
    }

    case HCMD_SET_ORIGIN:
        motorSetZero(X1_ADDR);
        motorSetZero(X2_ADDR);
        motorSetZero(Y_ADDR);
        g_cur_x = 0;
        g_cur_y = 0;
        osDelay(100);
        PrintDebug("[HOST] SET_ORIGIN\r\n");
        break;

    case HCMD_SET_SERVO:
        /* Z 轴舵机角度设定 (0~180) */
        {
            float angle = cmd->param;
            if (angle < 0.0f) angle = 0.0f;
            if (angle > 180.0f) angle = 180.0f;
            Servo_SetAngle(2, angle);
            PrintDebug("[HOST] SET_SERVO %.1f deg\r\n", angle);
        }
        break;

    case HCMD_EXIT_DEBUG:
        g_state = HOST_INIT;
        g_jog_active = false;
        PrintDebug("[HOST] Exit debug, back to IDLE.\r\n");
        host_send("EXIT_DEBUG_MODE");
        osDelay(50);
        host_send("DOWNLOAD_READY");
        break;

    default:
        break;
}

/* ================================================================
 *  PnP 子流程 — 每步调用一次 (非阻塞)
 * ================================================================ */

/* Mark 对齐一步 (HOST_MARK_ALIGN 时调用) */
static void mark_align_step(void) {
    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    switch (vs) {
    case VISION_GOT_STOP:
        /* 收到 stp，停电机 (此处无电机在动) → 发 go */
        Vision_Go();
        break;

    case VISION_GOT_POS: {
        /* 收到 Mark 偏移 → 记录并移动 */
        int32_t idx = r->mark_index;
        if (idx >= 0 && idx < P2_MARK_COUNT) {
            g_mark_offsets[idx][0] = r->dx;
            g_mark_offsets[idx][1] = r->dy;
            g_mark_count_done = idx + 1;
        }

        /* 移动：mm*10000 → 步数 */
        if (r->dx != 0 || r->dy != 0) {
            int32_t dx_s = (int32_t)(r->dx / 10000.0f * STEPS_PER_MM);
            int32_t dy_s = (int32_t)(r->dy / 10000.0f * STEPS_PER_MM);
            move_xy_relative(dx_s, dy_s, PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            PrintDebug("[HOST] Mark%d offset: (%ld,%ld)mm10000 → move(%ld,%ld)steps\r\n",
                       (int)idx, (long)r->dx, (long)r->dy, (long)dx_s, (long)dy_s);
        }
        Vision_Go();
        break;
    }

    case VISION_DONE: {
        /* 全部 3 个 Mark 完成 — 计算平均偏移 */
        if (g_mark_count_done >= 3) {
            g_mark_avg_dx = (g_mark_offsets[0][0] + g_mark_offsets[1][0] + g_mark_offsets[2][0]) / 3;
            g_mark_avg_dy = (g_mark_offsets[0][1] + g_mark_offsets[1][1] + g_mark_offsets[2][1]) / 3;
        }
        g_comp_index = 0;
        PrintDebug("[HOST] Mark alignment complete. %d marks. Avg offset: (%ld,%ld)mm10000\\r\\n",
                   g_mark_count_done, (long)g_mark_avg_dx, (long)g_mark_avg_dy);
        /* 移动到散料区起始位置，准备找元件 */
        move_xy_relative(FEEDER_AREA_X_STEPS - g_cur_x, FEEDER_AREA_Y_STEPS - g_cur_y,
                         PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
        Vision_Start(VCMD_P1);
        g_state = HOST_FIND_COMP;
        break;
    }

    case VISION_ERROR:
        PrintDebug("[HOST] Mark alignment ERROR: %s\r\n", Vision_GetError());
        g_state = HOST_ERROR;
        break;

    default:
        break;
    }
}

/* 找元件一步 (HOST_FIND_COMP 时调用) */
static void find_comp_step(void) {
    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    switch (vs) {
    case VISION_GOT_STOP:
        /* P1 Phase0: 目标锁定 → 停电机 → go */
        if (g_jog_active) { disable_sync_stop(); g_jog_active = false; }
        Vision_Go();
        break;

    case VISION_GOT_POS: {
        /* P1 Phase1 或 Phase2: 收到偏移数据 */
        /* TODO: 需根据上相机实际 FOV 校准像素→步数比例 */
        Component_t *c = &g_components[g_comp_index];
        int32_t dx_s = (int32_t)(r->dx * (STEPS_PER_MM / 1000.0f));
        int32_t dy_s = (int32_t)(r->dy * (STEPS_PER_MM / 1000.0f));

        /* Phase1 独有：记录角度和类别 */
        if (r->angle_x100 != 0 || r->class_name[0] != '\0') {
            PrintDebug("[HOST] Comp %u: cls=%s ang=%ld.%02ddeg offset(%ld,%ld)px\r\n",
                       c->id, r->class_name,
                       (long)(r->angle_x100 / 100), (int)(r->angle_x100 % 100),
                       (long)r->dx, (long)r->dy);
        } else {
            PrintDebug("[HOST] Comp %u iter: offset(%ld,%ld)px\r\n",
                       c->id, (long)r->dx, (long)r->dy);
        }

        /* 移动 (如果偏移够大) */
        if (dx_s != 0 || dy_s != 0) {
            move_xy_relative(dx_s, dy_s, PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
        }
        Vision_Go();
        break;
    }

    case VISION_DONE:
        /* P1 完成：元件已对齐 → 吸取 */
        PrintDebug("[HOST] Comp %u aligned. Picking...\r\n",
                   g_components[g_comp_index].id);
        g_state = HOST_PICK;
        break;

    case VISION_ERROR:
        PrintDebug("[HOST] Find comp %u ERROR: %s\r\n",
                   g_components[g_comp_index].id, Vision_GetError());
        g_comp_index++;
        if (g_comp_index >= g_comp_count) {
            g_state = HOST_DONE;
        } else {
            Vision_Start(VCMD_P1);
        }
        break;

    default:
        break;
    }
}

/* 偏移检测一步 (HOST_OFFSET_CHECK 时调用) */
static void offset_check_step(void) {
    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    switch (vs) {
    case VISION_GOT_POS: {
        /* TODO: 需根据下相机实际 FOV 校准像素→步数比例 */
        int32_t dx_s = (int32_t)(r->dx * 0.1f);
        int32_t dy_s = (int32_t)(r->dy * 0.1f);
        if (dx_s != 0 || dy_s != 0) {
            move_xy_relative(dx_s, dy_s, PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
        }
        Vision_Go();
        break;
    }

    case VISION_DONE:
        /* P3 对齐完成 → 贴装 */
        PrintDebug("[HOST] Offset check done. Placing...\r\n");
        g_state = HOST_PLACE;
        break;

    case VISION_ERROR:
        PrintDebug("[HOST] Offset check ERROR: %s\r\n", Vision_GetError());
        g_state = HOST_PLACE;  /* 容错：仍然贴装 */
        break;

    default:
        break;
    }
}

/* ================================================================
 *  Host_UartRecvCallback — UART ISR 中调用
 * ================================================================ */
void Host_UartRecvCallback(uint8_t *data, int len) {
    if (host_pkt_queue == NULL) return;

    for (int i = 0; i < len; i++) {
        HostParsed_t parsed;
        if (LineParser_Feed(&g_parser, data[i], &parsed)) {
            HostMsg_t msg;
            msg.type = MSG_HOST_CMD;
            msg.host_cmd = parsed;
            osMessageQueuePut(host_pkt_queue, &msg, 0, 0);
        }
    }
}

/* ================================================================
 *  Host_Task — 统一主任务
 * ================================================================ */
void Host_Task(void *argument) {
    (void)argument;

    /* ---- 初始化 ---- */
    LineParser_Init(&g_parser);
    Vision_Init();
    g_state = HOST_INIT;
    g_comp_count = 0;
    g_comp_index = 0;
    g_header_parsed = false;
    g_cur_x = 0;
    g_cur_y = 0;
    g_jog_active = false;
    memset(g_components, 0, sizeof(g_components));
    memset(g_mark_offsets, 0, sizeof(g_mark_offsets));

    /* 电机初始化 */
    CAN_Init(&hfdcan1, NULL);
    Motor_Init();
    osDelay(200);

    PrintDebug("[HOST] Task started.\r\n");

    /* 启动握手：发送 DEBUG_MODE，进入调试模式 */
    UART_SendString(UART_CH1, "DEBUG_MODE\n");
    osDelay(50);
    UART_SendString(UART_CH1, "DOWNLOAD_READY\n");
    g_state = HOST_DEBUG;



    /* ===== 主循环 ===== */
    for (;;) {
        /* ---- 1. 驱动 UART DMA 接收 ---- */
        UART_Driver_Process();

        /* ---- 2. 从队列读取 ISR 回调已解析的命令 ---- */
        {
            HostMsg_t msg;
            while (osMessageQueueGet(host_pkt_queue, &msg, NULL, 0) == osOK) {
                if (msg.type != MSG_HOST_CMD) continue;
                HostParsed_t *parsed = &msg.host_cmd;

                /* 调试模式：CSV 数据 → 切换到下载模式 */
                if ((g_state == HOST_DEBUG || g_state == HOST_INIT) && parsed->cmd == HCMD_RAW_LINE) {
                    g_state = HOST_DOWNLOADING;
                    g_comp_count = 0;
                    g_header_parsed = false;
                    PrintDebug("[HOST] Download started (from debug).\r\n");
                }

                /* 下载模式：处理 CSV 行 */
                if (g_state == HOST_DOWNLOADING) {
                    if (parsed->cmd == HCMD_RAW_LINE) {
                        g_last_line_tick = osKernelGetTickCount();
                        if (!g_header_parsed) {
                            parse_header(parsed->raw, parsed->raw_len);
                            g_header_parsed = true;
                        } else {
                            parse_csv_line(parsed->raw, parsed->raw_len);
                        }
                    } else {
                        PrintDebug("[HOST] WARN: non-CSV cmd %d dropped in download\r\n",
                                   (int)parsed->cmd);
                    }
                    continue;
                }

                /* 调试模式：处理运动命令 */
                if (g_state == HOST_DEBUG) {
                    handle_debug_cmd(parsed);
                }
            }
            /* 消费 ISR 已处理的数据，释放 UART 环形缓冲区 */
            UART_ClearData(UART_CH1);
        }
        /* ---- 3. 下载超时检测 ---- */
        if (g_state == HOST_DOWNLOADING) {
            uint32_t elapsed = osKernelGetTickCount() - g_last_line_tick;
            if (elapsed >= DOWNLOAD_TIMEOUT_MS) {
                download_done();
                continue;
            }
        }

        /* ---- 4. 状态机 ---- */
        switch (g_state) {

        case HOST_INIT:
            /* 空闲：等上位机发 CSV 或停留在调试 */
            osDelay(50);
            break;

        case HOST_DEBUG:
            osDelay(10);
            break;

        case HOST_DOWNLOADING:
            osDelay(10);
            break;

        case HOST_MARK_ALIGN:
            mark_align_step();
            break;

        case HOST_FIND_COMP:
            find_comp_step();
            break;

        case HOST_PICK: {
            Component_t *c = &g_components[g_comp_index];
            PrintDebug("[HOST] PICK comp %u (angle=%.1f)\r\n", c->id, c->target_angle);
            pick_component();
            /* 移动到下相机站检测偏移 */
            move_xy_relative(BOTTOM_CAM_X_STEPS - g_cur_x, BOTTOM_CAM_Y_STEPS - g_cur_y,
                             PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            Vision_Start(VCMD_P3);
            g_state = HOST_OFFSET_CHECK;
            break;
        }

        case HOST_OFFSET_CHECK:
            offset_check_step();
            break;

        case HOST_PLACE: {
            Component_t *c = &g_components[g_comp_index];
            /* 应用 Mark 对齐偏移到 PCB 目标坐标 */
            float adj_x = c->target_x + (float)g_mark_avg_dx / 10000.0f;
            float adj_y = c->target_y + (float)g_mark_avg_dy / 10000.0f;
            int32_t pcb_x = (int32_t)(adj_x * STEPS_PER_MM);
            int32_t pcb_y = (int32_t)(adj_y * STEPS_PER_MM);
            PrintDebug("[HOST] PLACE comp %u: target(%.1f,%.1f)+Mark->(%ld,%ld)steps, angle=%.1f\r\n",
                       c->id, c->target_x, c->target_y, (long)pcb_x, (long)pcb_y, c->target_angle);
            /* R 轴旋转到目标角度 */
            r_axis_rotate(c->target_angle, R_SPEED_RPM);
            /* XY 移动到 PCB 目标位置 */
            move_xy_relative(pcb_x - g_cur_x, pcb_y - g_cur_y,
                             PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            /* 贴装 */
            place_component();
            c->placed = true;
            g_comp_index++;
            if (g_comp_index >= g_comp_count) {
                g_state = HOST_DONE;
            } else {
                /* 回到散料区，准备找下一个元件 */
                move_xy_relative(FEEDER_AREA_X_STEPS - g_cur_x, FEEDER_AREA_Y_STEPS - g_cur_y,
                                 PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
                Vision_Start(VCMD_P1);
                g_state = HOST_FIND_COMP;
            }
            break;
        }

        case HOST_DONE:
            PrintDebug("[HOST] All %u components placed!\r\n", g_comp_count);
            g_comp_count = 0;
            osDelay(200);
            host_send("DOWNLOAD_READY");
            g_state = HOST_DEBUG;
            break;

        case HOST_ERROR:
            PrintDebug("[HOST] ERROR state. Resetting to DEBUG.\r\n");
            g_comp_count = 0;
            g_comp_index = 0;
            osDelay(500);
            host_send("DOWNLOAD_READY");
            g_state = HOST_DEBUG;
            break;

        default:
            break;
        }

        osDelay(5);
    }
}
