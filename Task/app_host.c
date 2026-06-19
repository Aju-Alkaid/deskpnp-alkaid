#include "app_host.h"
#include "app_test.h"
#include "driver_uart.h"
#include "driver_can.h"
#include "driver_motor.h"
#include "driver_servo.h"
#include "driver_drv8803.h"
#include "driver_tmc2209.h"
#include "driver_heater.h"
#include "app_logger.h"
#include "driver_spiflash_w25q64.h"

extern TIM_HandleTypeDef htim2;  /* Z轴舵机 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ================================================================
 *  常量
 * ================================================================ */
#define DEBUG_SPEED     300            /* 调试模式离散移动速度 */
#define DEBUG_ACC       25             /* 调试模式加速度 */
#define JOG_SPEED        300            /* 连续移动速度 (RPM) */
#define JOG_ACC          25             /* 连续移动加速度 */
#define JOG_MMS_TO_RPM  12.0f   /* mm/s → RPM: STEPS_PER_MM/16384*60 */
#define PNP_SPEED        300   /* 通用速度 (RPM) */
#define PNP_ACC          25    /* 通用加速度 */
#define PNP_SPEED_FAST   300   /* 长途移动 */
#define PNP_SPEED_FINE   100   /* 视觉迭代微调 */
#define PNP_ACC_FINE     10    /* 微调加速度 */
#define PICK_DELAY_MS    300
#define PLACE_DELAY_MS   300
#define PUMP_BLOW_MS    1000          /* 关气泵后电磁阀吹气时长(ms) */

#define MARK_VERIFY_ERR_MM  0.3f   /* Mark3 验证允许误差 (mm) */
#define P2_SCAN_STEP_MM      9.0f   /* P2 网格扫描步长 (mm) */
#define P2_SCAN_TIMEOUT       300   /* 每格位超时 (~3s, 主循环10ms/轮) */
#define Z_SERVO_CH       2            /* 舵机通道号 */

/* ================================================================
 *  任务内全局状态
 * ================================================================ */
static HostState_t  g_state = HOST_HOME;
static Component_t  g_components[MAX_COMPONENTS];
static uint16_t     g_comp_count = 0;
static uint16_t     g_comp_index = 0;

/* 下载超时检测 */
static uint32_t     g_last_line_tick = 0;
static bool         g_header_parsed = false;

/* Mark 点 (SMD="MARK") 独立存储 */
static Component_t  g_marks[MAX_MARKS];
static uint16_t     g_mark_count = 0;

/* 当前坐标 (步数) */
static int32_t g_cur_x = 0;
static int32_t g_cur_y = 0;

/* 标定数据 (Flash 持久化) */
CalibrationData_t g_calib;

/* JOG 状态 */
static bool g_during_cmd = false;  /* 正在执行命令时置位，用于屏蔽回显 */
static bool g_jog_active = false;
static bool g_auto_heat = false;  /* AUTO_HEAT ON/OFF */
/* 命令去重：连续两次相同 cmd+param 直接丢弃（中间有别的命令会复位） */
static HostCmd_t    g_last_cmd = HCMD_NONE;
static float        g_last_param = 0.0f;


/* 行解析器 */
static LineParser_t g_parser;


/* Mark 对齐累计偏移 */
static int32_t g_mark_offsets[P2_MARK_COUNT][2];  /* 3个Mark的(dx, dy) mm*10000 */

static int32_t g_mark_count_done = 0;

/* Mark 对齐平均偏移 (mm*10000)，PLACE 时应用到贴装坐标 */
static int32_t g_mark_avg_dx = 0;
static int32_t g_mark_avg_dy = 0;
static int     g_p1_retry_count = 0;  /* P1重试计数 */
static int     g_consecutive_failures = 0;  /* 连续元件失败计数 */

/* PCB 坐标系 (P2 建系结果) */
PCBFrame_t g_pcb_frame = {0};

/* Mark 实际机器坐标 (步数)，建系时填入 */
static int32_t g_marks_actual[P2_MARK_COUNT][2];

/* P3 下相机偏移累积 (步数) */
static int32_t g_p3_offset_x = 0;
static int32_t g_p3_offset_y = 0;

/* P2 引导式扫描状态 */
static bool    g_mark_scanning = false;
static int32_t g_scan_cols = 0, g_scan_rows = 0;
static int32_t g_scan_cur  = 0;
static int     g_scan_timeout = 0;
static bool    g_mark_just_jumped = false;  /* 防止 Mark 跳转后冗余二次跳转 */


/* 散料区子扫描位: [cell][subpos][x/y] */
static int32_t g_scatter_subpos[SCATTER_CELLS][SCATTER_SUBPOS][2];
static int     g_p1_scan_pos = 0;   /* P1 当前子扫描位 0..4 */

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

/* ---- CSV 解析（v2：制表符分隔、15 固定列、引号包裹、mm 后缀）---- */


/* ================================================================
 *  散料区单元格 + 子扫描位预计算
 * ================================================================ */
static void scatter_init_cells(void) {
    int32_t sx = g_calib.scatter_x_steps;
    int32_t sy = g_calib.scatter_y_steps;
    int32_t size = g_calib.scatter_size_steps;
    /* 单元格中心偏移: ±size/4 */
    int32_t co[4][2] = {
        {-size/4, -size/4},  /* cell 0: 左上 */
        {+size/4, -size/4},  /* cell 1: 右上 */
        {-size/4, +size/4},  /* cell 2: 左下 */
        {+size/4, +size/4},  /* cell 3: 右下 */
    };
    /* 子位偏移 (相对格中心): 中心 → 左上 → 右上 → 右下 → 左下 顺时针 */
    int32_t so[5][2] = {
        {       0,        0},  /* 0: 中心 */
        {-size/8, -size/8},  /* 1: 左上 */
        {+size/8, -size/8},  /* 2: 右上 */
        {+size/8, +size/8},  /* 3: 右下 */
        {-size/8, +size/8},  /* 4: 左下 */
    };
    for (int cell = 0; cell < SCATTER_CELLS; cell++) {
        for (int sp = 0; sp < SCATTER_SUBPOS; sp++) {
            g_scatter_subpos[cell][sp][0] = sx + co[cell][0] + so[sp][0];
            g_scatter_subpos[cell][sp][1] = sy + co[cell][1] + so[sp][1];
        }
    }
    PrintDebug("[HOST] Scatter cells: size=%ld, subpos computed.\r\n", (long)size);
}


/* 封装名->P1类别ID映射 (0=ccapt 1=cledy 2=cledo 3=crest) */
static int footprint_to_class_id(const char *fp) {
    if (!fp) return 0;
    if (strncmp(fp, "LED", 3) == 0) return 2;   /* LED-SMD -> cledo */
    if (strncmp(fp, "C0", 2) == 0 || strncmp(fp, "R0", 2) == 0) return 0;
    return 0;
}

/* 元件→单元格编号 (P1 class_id 即 cell 编号) */
static int component_cell(const Component_t *c) {
    int id = footprint_to_class_id(c->footprint);
    if (id < 0 || id >= SCATTER_CELLS) id = 0;
    return id;
}

/*
 * 提取第 n 个制表符分隔字段，去除首尾引号，写入 out
 */
static bool csv_get_field(const char *line, uint16_t len, int n,
                          char *out, uint16_t out_max) {
    if (n < 0) return false;
    const char *p = line;
    const char *end = line + len;
    int col = 0;
    while (col < n && p < end) {
        p = memchr(p, '\t', (uint16_t)(end - p));
        if (p == NULL) return false;
        p++;
        col++;
    }
    if (p >= end) return false;
    const char *next = memchr(p, '\t', (uint16_t)(end - p));
    uint16_t flen = next ? (uint16_t)(next - p) : (uint16_t)(end - p);
    if (flen >= 2 && p[0] == '"' && p[flen - 1] == '"') {
        p++;
        flen -= 2;
    }
    if (flen >= out_max) flen = out_max - 1;
    memcpy(out, p, flen);
    out[flen] = '\0';
    return true;
}

/*
 * 解析 "8mm" -> 8.0f
 */
static float parse_mm(const char *str) {
    char buf[32];
    uint16_t len = (uint16_t)strlen(str);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, str, len);
    buf[len] = '\0';
    if (len >= 2 && buf[len - 2] == 'm' && buf[len - 1] == 'm') {
        buf[len - 2] = '\0';
    }
    return strtof(buf, NULL);
}

/*
 * 解析一行 CSV（15 列，制表符分隔）
 * 列: [0]Designator [1]Device [2]Footprint [3]Mid X [4]Mid Y
 *     [5]Ref X [6]Ref Y [7]Pad X [8]Pad Y [9]Pins
 *     [10]Layer [11]Rotation [12]SMD [13]Comment [14]Name
 */
static bool parse_csv_line(const char *line, uint16_t len) {
    if (!g_header_parsed) {
        if (len >= 12 && memcmp(line, "\"Designator\"", 12) == 0) {
            g_header_parsed = true;
            PrintDebug("[HOST] Header detected, skipping.\r\n");
            return false;
        }
        g_header_parsed = true;
    }

    char smd[8] = {0};
    if (!csv_get_field(line, len, 12, smd, sizeof(smd))) {
        return false;
    }

    bool is_mark = (strcmp(smd, "MARK") == 0);
    if (!is_mark && strcmp(smd, "Yes") != 0) {
        return false;
    }

    Component_t *c;
    if (is_mark) {
        if (g_mark_count >= MAX_MARKS) {
            PrintDebug("[HOST] WARN: too many marks (>%d)\r\n", MAX_MARKS);
            return false;
        }
        c = &g_marks[g_mark_count];
    } else {
        if (g_comp_count >= MAX_COMPONENTS) {
            PrintDebug("[HOST] WARN: too many components (>%d)\r\n", MAX_COMPONENTS);
            return false;
        }
        c = &g_components[g_comp_count];
    }

    memset(c, 0, sizeof(*c));
    c->feeder_id = 1;

    char tmp[32];

    if (csv_get_field(line, len, 2, tmp, sizeof(tmp))) {
        uint16_t n = (uint16_t)strlen(tmp);
        if (n >= sizeof(c->footprint)) n = sizeof(c->footprint) - 1;
        memcpy(c->footprint, tmp, n);
        c->footprint[n] = '\0';
    }

    if (csv_get_field(line, len, 3, tmp, sizeof(tmp))) {
        c->target_x = parse_mm(tmp);
    }

    if (csv_get_field(line, len, 4, tmp, sizeof(tmp))) {
        c->target_y = parse_mm(tmp);
    }

    if (csv_get_field(line, len, 10, tmp, sizeof(tmp))) {
        c->layer = (tmp[0] == 'T' || tmp[0] == 'B') ? tmp[0] : 'T';
    } else {
        c->layer = 'T';
    }

    if (csv_get_field(line, len, 11, tmp, sizeof(tmp))) {
        c->target_angle = strtof(tmp, NULL);
    }

    c->is_mark = is_mark;

    if (is_mark) {
        c->id = g_mark_count + 1;
        g_mark_count++;
    } else {
        c->id = g_comp_count + 1;
        g_comp_count++;
    }

    return true;
}

static void download_done(void) {
    PrintDebug("[HOST] Download done. %u marks, %u components.\r\n",
               g_mark_count, g_comp_count);
    {
        uint8_t d[8] = {0};
        d[0] = (uint8_t)(g_comp_count >> 8);
        d[1] = (uint8_t)(g_comp_count);
        d[2] = (uint8_t)(g_mark_count);
        Log_Write(LOG_PNP_START, d);
    }
    g_header_parsed = false;
    memset(&g_pcb_frame, 0, sizeof(g_pcb_frame));  /* 清除旧建系结果 */
    g_consecutive_failures = 0;
    g_mark_avg_dx = 0;
    g_mark_avg_dy = 0;

    if (g_mark_count > 0) {
        g_comp_index = 0;
        g_mark_count_done = 0;
        memset(g_mark_offsets, 0, sizeof(g_mark_offsets));
        memset(g_marks_actual, 0, sizeof(g_marks_actual));
        g_mark_just_jumped = false;
        g_state = HOST_MARK_ALIGN;

        /* 计算 P2 网格扫描参数 */
        int32_t scan_step = (int32_t)(P2_SCAN_STEP_MM * STEPS_PER_MM);
        int32_t area_w = g_calib.pcb_area_x_max - g_calib.pcb_area_x_min;
        int32_t area_h = g_calib.pcb_area_y_max - g_calib.pcb_area_y_min;
        if (area_w > 0 && area_h > 0) {
            g_scan_cols = (area_w + scan_step - 1) / scan_step;
            g_scan_rows = (area_h + scan_step - 1) / scan_step;
            if (g_scan_cols < 1) g_scan_cols = 1;
            if (g_scan_rows < 1) g_scan_rows = 1;
            g_scan_cur    = 0;
            g_scan_timeout = 0;
            g_mark_scanning = true;
            safe_move_to(g_calib.pcb_area_x_min, g_calib.pcb_area_y_min,
                         PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            PrintDebug("[HOST] P2 scan: %ldx%ld grid, step=%ld steps\r\n",
                       (long)g_scan_cols, (long)g_scan_rows, (long)scan_step);
        } else {
            g_mark_scanning = false;
            PrintDebug("[HOST] PCB area uncalibrated, single-spot P2.\r\n");
        }

        Vision_Start(VCMD_P2, 0);
        Vision_Go();
        PrintDebug("[HOST] Starting Mark alignment (P2, %u marks)...\r\n", g_mark_count);
    } else if (g_comp_count > 0) {
        g_comp_index = 0;
        { int cl = component_cell(&g_components[0]);
          g_p1_scan_pos = 0;
          safe_move_to(g_scatter_subpos[cl][0][0], g_scatter_subpos[cl][0][1],
                       PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y); }
        Vision_Start(VCMD_P1, footprint_to_class_id(g_components[0].footprint));
        g_state = HOST_FIND_COMP;
        PrintDebug("[HOST] No marks, starting find component (P1)...\r\n");
    } else {
        host_send("DOWNLOAD_READY");
        g_state = HOST_DEBUG;
    }
}

/* ================================================================
 *  调试模式命令处理
 * ================================================================ */
static void handle_debug_cmd(HostParsed_t *cmd) {
    /* JOG dedup only: block repeated START with same speed.
     * STOP or direction change resets.
     * Discrete moves always execute -- no dedup. */
    if ((cmd->cmd == HCMD_MOVE_UP_START   || cmd->cmd == HCMD_MOVE_DOWN_START ||
         cmd->cmd == HCMD_MOVE_LEFT_START || cmd->cmd == HCMD_MOVE_RIGHT_START) &&
        cmd->cmd == g_last_cmd && cmd->param == g_last_param) {
        return;
    }
    g_last_cmd  = cmd->cmd;
    g_last_param = cmd->param;
    g_during_cmd = true;


    switch (cmd->cmd) {
    case HCMD_MOVE_UP:    /* fall through — 离散移动四方向统一处理 */
    case HCMD_MOVE_DOWN:
    case HCMD_MOVE_LEFT:
    case HCMD_MOVE_RIGHT:
    {
        /* 查表：{dx_sign, dy_sign} 映射到 (X步数, Y步数) */
        static const struct { int8_t sx; int8_t sy; const char *name; } tbl[] = {
            [HCMD_MOVE_UP    - HCMD_MOVE_UP] = { 1, 0, "MOVE_UP"    },
            [HCMD_MOVE_DOWN  - HCMD_MOVE_UP] = {-1, 0, "MOVE_DOWN"  },
            [HCMD_MOVE_LEFT  - HCMD_MOVE_UP] = { 0, 1, "MOVE_LEFT"  },
            [HCMD_MOVE_RIGHT - HCMD_MOVE_UP] = { 0,-1, "MOVE_RIGHT" },
        };
        uint8_t idx = (uint8_t)(cmd->cmd - HCMD_MOVE_UP);
        int32_t steps_mm = (int32_t)(cmd->param * STEPS_PER_MM);
        int32_t dx = tbl[idx].sx * steps_mm;
        int32_t dy = tbl[idx].sy * steps_mm;
        int ret = safe_move_to(g_cur_x + dx, g_cur_y + dy, DEBUG_SPEED, DEBUG_ACC, &g_cur_x, &g_cur_y);
        g_jog_active = false;
        if (ret < 0) {
            PrintDebug("[HOST] %s %.1fmm INTERRUPTED(ret=%d) pos=(%ld,%ld)\r\n",
                       tbl[idx].name, cmd->param, ret, g_cur_x, g_cur_y);
        } else {
            PrintDebug("[HOST] %s %.1fmm -> (%ld,%ld)\r\n",
                       tbl[idx].name, cmd->param, g_cur_x, g_cur_y);
        }
        break;
    }

    case HCMD_MOVE_UP_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        positionMode3Run(X1_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, JOG_MAX_STEPS);
        positionMode3Run(X2_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG UP %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_DOWN_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        positionMode3Run(X1_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, -JOG_MAX_STEPS);
        positionMode3Run(X2_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, -JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG DOWN %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_LEFT_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        positionMode3Run(Y_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG LEFT %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_RIGHT_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
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
        safe_move_to(tx, ty, DEBUG_SPEED, DEBUG_ACC, &g_cur_x, &g_cur_y);
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
        if (g_state == HOST_HOME) {
            UART_SendString(UART_CH1, "DEBUG_MODE\n");
            g_state = HOST_DEBUG;
            PrintDebug("[HOST] Home done, entering DEBUG mode.\r\n");
        }
        break;

    case HCMD_SET_SERVO:
        /* Z 轴舵机角度设定 (0~180) */
        {
            float angle = cmd->param;
            if (angle < 0.0f) angle = 0.0f;
            if (angle > 180.0f) angle = 180.0f;
            Servo_SetAngle(Z_SERVO_CH, angle);
            PrintDebug("[HOST] SET_SERVO %.1f deg\r\n", angle);
        }
        break;

    case HCMD_SET_R_AXIS:
        /* R 轴旋转角度 (0~360) */
        {
            float angle = cmd->param;
            if (angle < 0.0f) angle = 0.0f;
            if (angle > 360.0f) angle = 360.0f;
            r_axis_rotate(angle, R_SPEED_RPM);
            PrintDebug("[HOST] SET_R_AXIS %.1f deg\r\n", angle);
        }
        break;

    case HCMD_PUMP_ON:
        Pump_On();
        PrintDebug("[HOST] PUMP_ON\r\n");
        break;

    case HCMD_PUMP_OFF:
        Pump_Off();                         /* 关气泵 */
        Valve_On();                         /* 开电磁阀吹气 (PA6=HIGH) */
        osDelay(PUMP_BLOW_MS);              /* 吹气 1s */
        Valve_Off();                        /* 关电磁阀 (PA6=LOW) */
        PrintDebug("[HOST] PUMP_OFF done\r\n");
        break;

    case HCMD_HEAT_ON:
        Heater_SendStart();
        PrintDebug("[HOST] HEAT_ON\r\n");
        break;

    case HCMD_HEAT_OFF:
        Heater_SendStop();
        PrintDebug("[HOST] HEAT_OFF\r\n");
        break;

    case HCMD_EXIT_DEBUG:
        g_state = HOST_DEBUG;
        g_jog_active = false;
        PrintDebug("[HOST] Exit debug, back to IDLE.\r\n");
        host_send("EXIT_DEBUG_MODE");
        osDelay(50);
        host_send("DOWNLOAD_READY");
        break;

    /* ---- 标定命令 ---- */
    case HCMD_SET_SCATTER_AREA:
        g_calib.scatter_x_steps = g_cur_x;
        g_calib.scatter_y_steps = g_cur_y;
        PrintDebug("[HOST] SET_SCATTER_AREA: (%ld,%ld)\r\n", (long)g_cur_x, (long)g_cur_y);
        break;

    case HCMD_SET_SCATTER_SIZE:
        g_calib.scatter_size_steps = (int32_t)(cmd->param * STEPS_PER_MM);
        PrintDebug("[HOST] SET_SCATTER_SIZE: %.1fmm -> %ld steps\r\n", cmd->param, (long)g_calib.scatter_size_steps);
        break;

    case HCMD_SET_PCB_AREA_MIN:
        g_calib.pcb_area_x_min = g_cur_x;
        g_calib.pcb_area_y_min = g_cur_y;
        PrintDebug("[HOST] SET_PCB_AREA_MIN: (%ld,%ld)\r\n", (long)g_cur_x, (long)g_cur_y);
        break;

    case HCMD_SET_PCB_AREA_MAX:
        g_calib.pcb_area_x_max = g_cur_x;
        g_calib.pcb_area_y_max = g_cur_y;
        PrintDebug("[HOST] SET_PCB_AREA_MAX: (%ld,%ld)\r\n", (long)g_cur_x, (long)g_cur_y);
        break;

    case HCMD_SET_BOTTOM_CAM:
        g_calib.bottom_cam_x_steps = g_cur_x;
        g_calib.bottom_cam_y_steps = g_cur_y;
        PrintDebug("[HOST] SET_BOTTOM_CAM: (%ld,%ld)\r\n", (long)g_cur_x, (long)g_cur_y);
        break;

    case HCMD_SET_Z_SAFE:
        {
            float ang = Servo_GetAngle(Z_SERVO_CH);
            if (ang >= 0.0f) g_calib.z_safe_angle = ang;
            PrintDebug("[HOST] SET_Z_SAFE: %.1f deg\r\n", g_calib.z_safe_angle);
        }
        break;

    case HCMD_SET_Z_PICK:
        {
            float ang = Servo_GetAngle(Z_SERVO_CH);
            if (ang >= 0.0f) g_calib.z_pick_angle = ang;
            PrintDebug("[HOST] SET_Z_PICK: %.1f deg\r\n", g_calib.z_pick_angle);
        }
        break;

    case HCMD_SET_Z_PLACE:
        {
            float ang = Servo_GetAngle(Z_SERVO_CH);
            if (ang >= 0.0f) g_calib.z_place_angle = ang;
            PrintDebug("[HOST] SET_Z_PLACE: %.1f deg\r\n", g_calib.z_place_angle);
        }
        break;

    case HCMD_SET_R_ZERO:
        r_axis_set_zero();
        PrintDebug("[HOST] SET_R_ZERO: R axis zeroed\r\n");
        break;

    case HCMD_SAVE_CALIB:
        if (Calib_Save(&g_calib) == 0) {
            PrintDebug("[HOST] SAVE_CALIB: saved to Flash.\r\n");
            host_send("CALIB_SAVED");
        } else {
            PrintDebug("[HOST] SAVE_CALIB: Flash write FAILED!\r\n");
            host_send("CALIB_SAVE_FAILED");
        }
        break;

    case HCMD_RESUME:
        if (g_state == HOST_WAIT_REFILL || g_state == HOST_ERROR) {
            g_consecutive_failures = 0;
            g_p1_scan_pos = 0;
            Component_t *rc = &g_components[g_comp_index];
            int cl = component_cell(rc);
            safe_move_to(g_scatter_subpos[cl][0][0], g_scatter_subpos[cl][0][1],
                         PNP_SPEED_FAST, PNP_ACC, &g_cur_x, &g_cur_y);
            Vision_Start(VCMD_P1, footprint_to_class_id(rc->footprint));
            g_state = HOST_FIND_COMP;
            PrintDebug("[HOST] RESUME: retrying comp %u at cell %d\r\n", rc->id, cl);
        }
        break;

    case HCMD_AUTO_HEAT:
    if (strstr(cmd->raw, "ON"))  { g_auto_heat = true;  PrintDebug("[HOST] AUTO_HEAT ON\r\n");  host_send("AUTO_HEAT ON"); }
    if (strstr(cmd->raw, "OFF")) { g_auto_heat = false; PrintDebug("[HOST] AUTO_HEAT OFF\r\n"); host_send("AUTO_HEAT OFF"); }
    break;

    case HCMD_ABORT:
        /* 停止所有运动 */
        if (g_jog_active) { disable_sync_stop(); g_jog_active = false; }
        disable_sync_stop();
        nozzle_off();
        Valve_On(); osDelay(200); Valve_Off();
        z_safe();
        g_state = HOST_DEBUG;
        g_comp_count = 0;
        g_mark_count = 0;
        g_comp_index = 0;
        PrintDebug("[HOST] ABORT: motion stopped, back to DEBUG\r\n");
        Heater_SendStop();  /* 若回流焊进行中也停止 */
        { uint8_t d[8] = {0}; Log_Write(LOG_ABORT, d); }
        host_send("ABORT_OK");
        break;

    default:
        break;
    }
    g_during_cmd = false;
}

/* ================================================================
 *  PnP 子流程 — 每步调用一次 (非阻塞)
 * ================================================================ */

/* Mark 对齐一步 (HOST_MARK_ALIGN 时调用) */
static void mark_align_step(void) {
    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    if (Vision_IsTimedOut()) {
        PrintDebug("[HOST] Mark align timeout!\r\n");
        Vision_ForceIdle();
        g_state = HOST_ERROR;
        return;
    }

    switch (vs) {

    /* ---- 网格扫描 / 跳转搜索超时 ---- */
    case VISION_BUSY:
        g_scan_timeout++;
        if (g_scan_timeout < P2_SCAN_TIMEOUT) break;
        g_scan_timeout = 0;
        if (g_mark_scanning) {
            /* 网格扫描：移到下一格 */
            g_scan_cur++;
            if (g_scan_cur >= g_scan_cols * g_scan_rows) {
                PrintDebug("[HOST] P2 scan exhausted (%ld cells), Mark0 not found.\r\n",
                           (long)(g_scan_cols * g_scan_rows));
                g_mark_scanning = false;
                g_state = HOST_ERROR;
                break;
            }
            /* 蛇形扫描: 偶数行左→右，奇数行右→左 */
            int32_t row = g_scan_cur / g_scan_cols;
            int32_t col = g_scan_cur % g_scan_cols;
            int32_t step = (int32_t)(P2_SCAN_STEP_MM * STEPS_PER_MM);
            int32_t tx, ty;
            if (row & 1) {
                tx = g_calib.pcb_area_x_max - col * step;
            } else {
                tx = g_calib.pcb_area_x_min + col * step;
            }
            ty = g_calib.pcb_area_y_min + row * step;
            safe_move_to(tx, ty, PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            Vision_Start(VCMD_P2, 0);
            Vision_Go();
            PrintDebug("[HOST] P2 scan [%ld,%ld] → (%ld,%ld)\r\n",
                       (long)row, (long)col, (long)tx, (long)ty);
        } else {
            /* 跳转后搜索超时: Mark 不在预估位置 */
            PrintDebug("[HOST] P2 jump search timeout, mark not found.\r\n");
            g_state = HOST_ERROR;
        }
        break;

    /* ---- 收到 stp: Mark 锁定 ---- */
    case VISION_GOT_STOP:
        g_mark_scanning = false;   /* 停止网格扫描 */
        g_scan_timeout  = 0;
        {
            int32_t idx = r->mark_index;
            if (idx >= 1 && idx < P2_MARK_COUNT && !g_mark_just_jumped) {
                /* 首次 GOT_STOP (上一 Mark 完成): 跳转到预估位置 */
                float tdx = g_marks[idx].target_x - g_marks[idx-1].target_x;
                float tdy = g_marks[idx].target_y - g_marks[idx-1].target_y;
                int32_t dx = (int32_t)(tdx * STEPS_PER_MM);
                int32_t dy = (int32_t)(tdy * STEPS_PER_MM);
                int32_t prev = idx - 1;
                safe_move_to(g_marks_actual[prev][0] + dx,
                             g_marks_actual[prev][1] + dy,
                             PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
                g_mark_just_jumped = true;
                PrintDebug("[HOST] P2 jump Mark%ld: theory(%.1f,%.1f)mm → (%ld,%ld)\r\n",
                           (long)idx, tdx, tdy, (long)g_cur_x, (long)g_cur_y);
            }
            /* Mark0 或二次 GOT_STOP (相机已找到): 龙门已在目标位，无需移动 */
        }
        Vision_Go();
        break;

    /* ---- 收到偏移 → 移动并对齐 (保持不变) ---- */
    case VISION_GOT_POS: {
        g_mark_just_jumped = false;   /* 进入对齐阶段，清除跳转标志 */
        int32_t idx = r->mark_index;
        if (idx >= 0 && idx < P2_MARK_COUNT) {
            if (r->dx != 0 || r->dy != 0) {
                int32_t dx_s = -(int32_t)(r->dy / 10000.0f * STEPS_PER_MM);  // cam Y → X1+X2
                int32_t dy_s = -(int32_t)(r->dx / 10000.0f * STEPS_PER_MM);  // cam X → Y 电机
                safe_move_to(g_cur_x + dx_s, g_cur_y + dy_s, PNP_SPEED_FINE, PNP_ACC_FINE, &g_cur_x, &g_cur_y);
                PrintDebug("[HOST] Mark%ld offset: (%ld,%ld)mm10000 → move(%ld,%ld)steps\r\n",
                           (long)idx, (long)r->dx, (long)r->dy, (long)dx_s, (long)dy_s);
            }
            g_marks_actual[idx][0] = g_cur_x;
            g_marks_actual[idx][1] = g_cur_y;
            g_mark_offsets[idx][0] = r->dx;
            g_mark_offsets[idx][1] = r->dy;
            g_mark_count_done = idx + 1;
        }
        Vision_Go();
        break;
    }

    /* ---- 建系 (保持不变) ---- */
    case VISION_DONE: {
        g_mark_scanning = false;
        int32_t a1x = g_marks_actual[0][0], a1y = g_marks_actual[0][1];
        int32_t a2x = g_marks_actual[1][0], a2y = g_marks_actual[1][1];
        int32_t a3x = g_marks_actual[2][0], a3y = g_marks_actual[2][1];

        float t1x = g_marks[0].target_x, t1y = g_marks[0].target_y;
        float t2x = g_marks[1].target_x, t2y = g_marks[1].target_y;
        float t3x = g_marks[2].target_x, t3y = g_marks[2].target_y;

        float theory_ang = atan2f(t2y - t1y, t2x - t1x);
        float actual_ang = atan2f((float)(a2y - a1y), (float)(a2x - a1x));
        float theta = actual_ang - theory_ang;

        float mt_x = (t1x + t2x) * 0.5f * STEPS_PER_MM;
        float mt_y = (t1y + t2y) * 0.5f * STEPS_PER_MM;
        int32_t ma_x = (a1x + a2x) / 2;
        int32_t ma_y = (a1y + a2y) / 2;

        float cos_t = cosf(theta), sin_t = sinf(theta);
        g_pcb_frame.origin_x_steps = ma_x - (int32_t)(mt_x * cos_t - mt_y * sin_t);
        g_pcb_frame.origin_y_steps = ma_y - (int32_t)(mt_x * sin_t + mt_y * cos_t);
        g_pcb_frame.rotation_rad = theta;

        int32_t t3x_s = (int32_t)(t3x * STEPS_PER_MM);
        int32_t t3y_s = (int32_t)(t3y * STEPS_PER_MM);
        int32_t pred_x = (int32_t)(t3x_s * cos_t - t3y_s * sin_t) + g_pcb_frame.origin_x_steps;
        int32_t pred_y = (int32_t)(t3x_s * sin_t + t3y_s * cos_t) + g_pcb_frame.origin_y_steps;
        int32_t err_x = pred_x - a3x, err_y = pred_y - a3y;
        float err_mm = sqrtf((float)(err_x*err_x + err_y*err_y)) / STEPS_PER_MM;
        g_pcb_frame.valid = (err_mm < MARK_VERIFY_ERR_MM);

        PrintDebug("[HOST] === PCB Frame ===\r\n");
        PrintDebug("[HOST] origin=(%ld,%ld) theta=%.4frad(%.2fdeg)\r\n",
                   (long)g_pcb_frame.origin_x_steps, (long)g_pcb_frame.origin_y_steps,
                   theta, theta * 57.29578f);
        PrintDebug("[HOST] Mark3 verify: err=%.3fmm %s\r\n", err_mm,
                   g_pcb_frame.valid ? "OK" : "FAIL");

        if (g_mark_count_done >= 3) {
            g_mark_avg_dx = (g_mark_offsets[0][0] + g_mark_offsets[1][0] + g_mark_offsets[2][0]) / 3;
            g_mark_avg_dy = (g_mark_offsets[0][1] + g_mark_offsets[1][1] + g_mark_offsets[2][1]) / 3;
        }

        g_comp_index = 0;
        if (g_comp_count > 0) {
            { int cl = component_cell(&g_components[0]);
              g_p1_scan_pos = 0;
              safe_move_to(g_scatter_subpos[cl][0][0], g_scatter_subpos[cl][0][1],
                           PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y); }
            Vision_Start(VCMD_P1, footprint_to_class_id(g_components[0].footprint));
            g_state = HOST_FIND_COMP;
        } else {
            host_send("DOWNLOAD_READY");
            g_state = HOST_DEBUG;
        }
        break;
    }

    case VISION_ERROR:
        g_mark_scanning = false;
        PrintDebug("[HOST] Mark alignment ERROR: %s\r\n", Vision_GetError());
        g_state = HOST_ERROR;
        break;

    default:
        break;
    }
}

/* ---- R 轴角度矫正辅助：从视觉结果提取角度，超过阈值则执行 r_axis_rotate ---- */
static bool host_correct_r_from_vision(const VisionResult_t *r, const char *stage) {
    if (!r || !r->angle_valid) return false;
    float ang = (float)r->angle_x100 / 100.0f;
    if (fabsf(ang) <= R_CORRECTION_THRESHOLD_DEG) return false;
    PrintDebug("[HOST] %s: R correction %.2f deg\r\n", stage, (double)ang);
    r_axis_rotate(ang, R_SPEED_RPM);
    return true;
}

/* 找元件一步 (HOST_FIND_COMP 时调用) */
static void find_comp_step(void) {
    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    if (Vision_IsTimedOut()) {
        Vision_ForceIdle();
        Component_t *c = &g_components[g_comp_index];
        PrintDebug("[HOST] Find comp %u TIMEOUT, scan pos %d/%d\r\n",
                   c->id, g_p1_scan_pos, SCATTER_SUBPOS - 1);
        if (g_p1_scan_pos < SCATTER_SUBPOS - 1) {
            g_p1_scan_pos++;
            int cl = component_cell(c);
            safe_move_to(g_scatter_subpos[cl][g_p1_scan_pos][0],
                         g_scatter_subpos[cl][g_p1_scan_pos][1],
                         PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            Vision_Start(VCMD_P1, footprint_to_class_id(c->footprint));
            return;
        }
        /* 所有子位耗尽 → 跳过该元件 */
        g_p1_scan_pos = 0;
        g_consecutive_failures++;
        if (g_consecutive_failures >= 3) {
            host_send("REFILL_NEEDED");
            g_state = HOST_WAIT_REFILL;
        } else {
            g_comp_index++;
            if (g_comp_index >= g_comp_count) { g_state = HOST_DONE; }
            else {
                int cl = component_cell(&g_components[g_comp_index]);
                safe_move_to(g_scatter_subpos[cl][0][0], g_scatter_subpos[cl][0][1],
                             PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
                Vision_Start(VCMD_P1, footprint_to_class_id(g_components[g_comp_index].footprint));
            }
        }
        return;
    }

    switch (vs) {
    case VISION_GOT_CATEGORY_QUERY:
        /* P1 类别询问：Cam 需要知道元件类别，回复 cls */
        Vision_ClsReply();
        break;
    case VISION_GOT_STOP:
        /* P1 Phase0: 目标锁定 → 停电机 → go */
        if (g_jog_active) { disable_sync_stop(); g_jog_active = false; }
        g_p1_scan_pos = 0;   /* 找到目标，复位扫描位 */
        Vision_Go();
        break;

    case VISION_GOT_POS: {
        /* P1 Phase1 或 Phase2: 收到偏移数据 */
        /* TODO: 需根据上相机实际 FOV 校准像素→步数比例 */
        Component_t *c = &g_components[g_comp_index];
        int32_t dx_s = -(int32_t)(r->dy * (STEPS_PER_MM / 1000.0f));  // cam Y → X1+X2
        int32_t dy_s = -(int32_t)(r->dx * (STEPS_PER_MM / 1000.0f));  // cam X → Y 电机

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
            safe_move_to(g_cur_x + dx_s, g_cur_y + dy_s, PNP_SPEED_FINE, PNP_ACC_FINE, &g_cur_x, &g_cur_y);
        }
        Vision_Go();
        break;
    }

    case VISION_DONE:
        /* P1 完成 → R 轴矫正 → 吸取 */
        g_p1_retry_count = 0;
        g_p1_scan_pos = 0;
        g_consecutive_failures = 0;
        host_correct_r_from_vision(r, "P1");
        PrintDebug("[HOST] Comp %u aligned. Picking...\r\n",
                   g_components[g_comp_index].id);
        g_state = HOST_PICK;
        break;

    case VISION_ERROR: {
        const char *err = Vision_GetError();
        Component_t *c = &g_components[g_comp_index];
        bool is_not_found = (strcmp(err, "err1_5") == 0);
        bool recoverable  = is_not_found ||
                            (strcmp(err, "err1_8") == 0) ||
                            (strcmp(err, "err1_9") == 0);
        bool cam_fault    = (strcmp(err, "err1_1") == 0 ||
                             strcmp(err, "err1_4") == 0);

        /* 非"未找到"错误：在同一位置重试 */
        if (!is_not_found && (recoverable || cam_fault) && g_p1_retry_count < 3) {
            g_p1_retry_count++;
            PrintDebug("[HOST] Comp %u %s, retry P1 (%d/3)...\r\n",
                       c->id, err, g_p1_retry_count);
            Vision_Start(VCMD_P1, footprint_to_class_id(c->footprint));
        }
        /* "未找到"(err1_5)：尝试下一个子扫描位 */
        else if (is_not_found && g_p1_scan_pos < SCATTER_SUBPOS - 1) {
            g_p1_scan_pos++;
            g_p1_retry_count = 0;
            int cl = component_cell(c);
            safe_move_to(g_scatter_subpos[cl][g_p1_scan_pos][0],
                         g_scatter_subpos[cl][g_p1_scan_pos][1],
                         PNP_SPEED_FINE, PNP_ACC_FINE, &g_cur_x, &g_cur_y);
            PrintDebug("[HOST] Comp %u not found, scan pos %d/%d\r\n",
                       c->id, g_p1_scan_pos, SCATTER_SUBPOS - 1);
            Vision_Start(VCMD_P1, footprint_to_class_id(c->footprint));
        }
        /* 所有重试 + 所有扫描位都失败 */
        else {
            PrintDebug("[HOST] Find comp %u ERROR: %s (retries=%d, scan=%d)\r\n",
                       c->id, err, g_p1_retry_count, g_p1_scan_pos);
            g_p1_retry_count = 0;
            g_p1_scan_pos = 0;
            g_consecutive_failures++;
            if (g_consecutive_failures >= 3) {
                PrintDebug("[HOST] %d consecutive failures, refill needed.\r\n", g_consecutive_failures);
                host_send("REFILL_NEEDED");
                g_state = HOST_WAIT_REFILL;
            } else {
                /* 跳过该元件，尝试下一个 */
                g_comp_index++;
                if (g_comp_index >= g_comp_count) {
                    g_state = HOST_DONE;
                } else {
                    int cl = component_cell(&g_components[g_comp_index]);
                    safe_move_to(g_scatter_subpos[cl][0][0], g_scatter_subpos[cl][0][1],
                                 PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
                    Vision_Start(VCMD_P1, footprint_to_class_id(g_components[g_comp_index].footprint));
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

/* 偏移检测一步 (HOST_OFFSET_CHECK 时调用) */
static void offset_check_step(void) {
    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    if (Vision_IsTimedOut()) {
        PrintDebug("[HOST] Offset check timeout, skipping to place\r\n");
        Vision_ForceIdle();
        g_state = HOST_MOVE_TO_PCB;
        return;
    }

    switch (vs) {
    case VISION_GOT_ERR_RETRY:
        /* err3_3 recoverable: resend go */
        Vision_Go();
        break;

    case VISION_GOT_POS: {
        /* TODO: 需根据下相机实际 FOV 校准像素→步数比例 */
        int32_t dx_s = (int32_t)(r->dy * (STEPS_PER_MM / 1000.0f));   // cam Y → X1+X2，不取反
        int32_t dy_s = -(int32_t)(r->dx * (STEPS_PER_MM / 1000.0f));  // cam X → Y 电机，取反
        if (dx_s != 0 || dy_s != 0) {
            safe_move_to(g_cur_x + dx_s, g_cur_y + dy_s, PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            g_p3_offset_x += dx_s;
            g_p3_offset_y += dy_s;
        }
        Vision_Go();
        break;
    }

    case VISION_DONE:
        /* P3 完成 → 二次 R 轴矫正 → 计算 PCB 坐标 */
        host_correct_r_from_vision(r, "P3");
        PrintDebug("[HOST] Offset check done, moving to PCB...\r\n");
        g_state = HOST_MOVE_TO_PCB;
        break;

    case VISION_ERROR:
        PrintDebug("[HOST] Offset check ERROR: %s\r\n", Vision_GetError());
        g_state = HOST_MOVE_TO_PCB;  /* 容错：仍然贴装 */
        break;

    default:
        break;
    }
}

/* ================================================================
 *  Host_UartRecvCallback — UART ISR 中调用
 * ================================================================ */
/* ================================================================
 *  Host_UartRecvCallback — UART ISR 中调用（已弃用队列模式）
 *  仅保留 data_ready 标记，命令解析移至 Host_Task 主循环。
 * ================================================================ */
void Host_UartRecvCallback(uint8_t *data, int len) {
    (void)data; (void)len;
    /* 命令解析已移至 Host_Task 主循环（UART_PeekData），
     * 避免 ISR 中 FPU 浮点运算及 g_parser 竞态。 */
}


/* ================================================================
 *  Host_Task — 统一主任务
 * ================================================================ */
void Host_Task(void *argument) {
    (void)argument;

    /* ---- 初始化 ---- */
    LineParser_Init(&g_parser);
    Vision_Init();
    g_state = HOST_HOME;
    g_comp_count = 0;
    g_comp_index = 0;
    g_mark_count = 0;
    g_header_parsed = false;
    g_cur_x = 0;
    g_cur_y = 0;
    g_jog_active = false;
    memset(g_components, 0, sizeof(g_components));
    memset(g_marks, 0, sizeof(g_marks));
    memset(g_mark_offsets, 0, sizeof(g_mark_offsets));

    /* 电机初始化 */
    CAN_Init(&hfdcan1, NULL);
    Motor_Init();
    osDelay(200);

    /* 舵机 + DRV8803 初始化 */
    DRV8803_Init();
    DRV8803_EnableChip(1, true);   /* U12 12V 芯片使能 */
    DRV8803_EnableChip(2, true);   /* U13 24V 芯片使能（电磁阀等） */
    Servo_Init(&htim2);            /* Z轴舵机 TIM2_CH3 */
    DRV8803_SetOutput(&Port_12VO4, true);  /* 舵机上电 (12VO4) */
    Valve_Off();                        /* 电磁阀初始关断 (PA6=LOW) */
    osDelay(300);

    /* TMC2209 (R轴) 初始化 */
    if (!TMC_Init()) {
        PrintDebug("[HOST] TMC_Init failed!\r\n");
    }
    /* ENN 低有效：LOW=开启，HIGH=关闭。初始化完成后关闭，用到时再开 */
    TMC_SetEnable(false);


    
    /* 加热台初始化 */
    Heater_Init();
    Log_Init();

    /* 从 Flash 加载标定值 */
    if (Calib_Load(&g_calib) != 0) {
        PrintDebug("[HOST] Flash read failed, using defaults.\r\n");
    }
    scatter_init_cells();
    PrintDebug("[HOST] Calib: scatter=(%ld,%ld) botcam=(%ld,%ld)\r\n",
               (long)g_calib.scatter_x_steps, (long)g_calib.scatter_y_steps,
               (long)g_calib.bottom_cam_x_steps, (long)g_calib.bottom_cam_y_steps);

    PrintDebug("[HOST] Task started. Waiting for SET_ORIGIN...\r\n");



    /* ===== 主循环 ===== */
    for (;;) {
        /* ---- 1. 驱动 UART DMA 接收 ---- */
        UART_Driver_Process();

        /* ---- 2. 直接读取 UART 数据（任务上下文解析，避免 ISR FPU 问题） ---- */
        {
            const uint8_t *rx_data = NULL;
            uint16_t rx_len = 0;
            if (UART_PeekData(UART_CH1, &rx_data, &rx_len)) {
                for (uint16_t i = 0; i < rx_len; i++) {
                    HostParsed_t parsed;
                    if (LineParser_Feed(&g_parser, rx_data[i], &parsed)) {

                        /* 运动/调试命令：不受状态限制，始终处理 */
                        if (parsed.cmd != HCMD_RAW_LINE && parsed.cmd != HCMD_NONE &&
                            parsed.cmd != HCMD_UNKNOWN) {
                            handle_debug_cmd(&parsed);
                            continue;
                        }

                        /* 调试模式：RAW_LINE → 切换到文件下载模式
                         * 过滤回显：跳过调试输出和握手消息的环回
                         *   - 以 [ 开头 → PrintDebug 回显（"[HOST] ..."）
                         * 命令执行期间的 RAW_LINE = 回显，直接丢弃 */
                        if (g_during_cmd && parsed.cmd == HCMD_RAW_LINE) continue;
                        if ((g_state == HOST_DEBUG || g_state == HOST_HOME)
                            && parsed.cmd == HCMD_RAW_LINE
                            && parsed.raw_len > 0
                            && parsed.raw[0] != '['
                            && !(parsed.raw_len == 10 && memcmp(parsed.raw, "DEBUG_MODE", 10) == 0)
                            && !(parsed.raw_len == 14 && memcmp(parsed.raw, "DOWNLOAD_READY", 14) == 0)
                            && !(parsed.raw_len == 15 && memcmp(parsed.raw, "EXIT_DEBUG_MODE", 15) == 0)) {
                            g_state = HOST_DOWNLOADING;
                            g_comp_count = 0;
                            g_mark_count = 0;
                            g_header_parsed = false;
                            PrintDebug("[HOST] Download started (from debug).\r\n");
                        }

                        /* 下载模式：处理 CSV 行 */
                        if (g_state == HOST_DOWNLOADING) {
                            if (parsed.cmd == HCMD_RAW_LINE) {
                                g_last_line_tick = osKernelGetTickCount();
                                parse_csv_line(parsed.raw, parsed.raw_len);
                            } else {
                                PrintDebug("[HOST] WARN: non-CSV cmd %d dropped in download\r\n",
                                           (int)parsed.cmd);
                            }
                            continue;
                        }
                    }
                }
            }
            UART_ClearData(UART_CH1);
        }

        /* ---- 3. 加热台状态处理 ---- */
        Heater_ProcessStatus();

        /* ---- 4. 下载超时检测 ---- */
        if (g_state == HOST_DOWNLOADING) {
            uint32_t elapsed = osKernelGetTickCount() - g_last_line_tick;
            if (elapsed >= DOWNLOAD_TIMEOUT_MS) {
                download_done();
                continue;
            }
        }

        /* ---- 5. 状态机 ---- */
        switch (g_state) {

        case HOST_HOME:
            /* 等待 SET_ORIGIN 归零 */
            osDelay(100);
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
            if (!pick_component()) {
                PrintDebug("[HOST] Pick FAILED, restarting P1\r\n");
                int cl = component_cell(c);
                g_p1_scan_pos = 0;
                safe_move_to(g_scatter_subpos[cl][0][0], g_scatter_subpos[cl][0][1],
                             PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
                Vision_Start(VCMD_P1, footprint_to_class_id(c->footprint));
                g_state = HOST_FIND_COMP;
            } else {
                g_state = HOST_MOVE_TO_BOTTOM_CAM;
            }
            break;
        }

        case HOST_MOVE_TO_BOTTOM_CAM:
            g_p3_offset_x = 0;
            g_p3_offset_y = 0;
            safe_move_to(g_calib.bottom_cam_x_steps, g_calib.bottom_cam_y_steps,
                         PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            Vision_Start(VCMD_P3, 0);
            g_state = HOST_OFFSET_CHECK;
            break;

        case HOST_OFFSET_CHECK:
            offset_check_step();
            break;

        case HOST_MOVE_TO_PCB: {
            Component_t *c = &g_components[g_comp_index];
            int32_t cx = (int32_t)(c->target_x * STEPS_PER_MM);
            int32_t cy = (int32_t)(c->target_y * STEPS_PER_MM);
            int32_t machine_x, machine_y;
            if (g_pcb_frame.valid) {
                /* 旋转补偿 + PCB 原点平移 + P3 偏移 */
                float cos_t = cosf(g_pcb_frame.rotation_rad);
                float sin_t = sinf(g_pcb_frame.rotation_rad);
                machine_x = (int32_t)(cx * cos_t - cy * sin_t) + g_pcb_frame.origin_x_steps + g_p3_offset_x;
                machine_y = (int32_t)(cx * sin_t + cy * cos_t) + g_pcb_frame.origin_y_steps + g_p3_offset_y;
            } else {
                /* Fallback: Mark 平均偏移 (无旋转补偿) */
                machine_x = cx + (int32_t)(g_mark_avg_dx / 10000.0f * STEPS_PER_MM);
                machine_y = cy + (int32_t)(g_mark_avg_dy / 10000.0f * STEPS_PER_MM);
            }
            PrintDebug("[HOST] MOVE_TO_PCB comp %u: CSV(%.1f,%.1f)->machine(%ld,%ld) angle=%.1f\r\n",
                       c->id, c->target_x, c->target_y, (long)machine_x, (long)machine_y, c->target_angle);
            /* R 轴旋转到目标角度 */
            r_axis_rotate(c->target_angle, R_SPEED_RPM);
            /* XY 移动到贴装位 */
            safe_move_to(machine_x, machine_y, PNP_SPEED_FAST, PNP_ACC, &g_cur_x, &g_cur_y);
            g_state = HOST_PLACE;
            break;
        }

        case HOST_PLACE: {
            Component_t *c = &g_components[g_comp_index];
            place_component();
            c->placed = true;
            g_comp_index++;
            if (g_comp_index >= g_comp_count) {
                g_state = HOST_DONE;
            } else {
                /* 回到散料区，准备找下一个元件 */
                { int cl = component_cell(&g_components[g_comp_index]);
                  g_p1_scan_pos = 0;
                  safe_move_to(g_scatter_subpos[cl][0][0], g_scatter_subpos[cl][0][1],
                               PNP_SPEED_FAST, PNP_ACC, &g_cur_x, &g_cur_y); }
                Vision_Start(VCMD_P1, footprint_to_class_id(g_components[g_comp_index].footprint));
                g_state = HOST_FIND_COMP;
            }
            break;
        }
        case HOST_DONE:
            PrintDebug("[HOST] All %u components placed!\r\n", g_comp_count);
            {
                uint8_t d[8] = {0};
                d[0] = (uint8_t)(g_comp_count >> 8);
                d[1] = (uint8_t)(g_comp_count);
                Log_Write(LOG_PNP_DONE, d);
            }            g_comp_count = 0;
            g_mark_count = 0;
            /* 回机器零点 */
            safe_move_to(0, 0, PNP_SPEED_FAST, PNP_ACC, &g_cur_x, &g_cur_y);
            osDelay(200);
            if (g_auto_heat) {
                Heater_SendStart();
                { uint8_t d[8] = {0}; Log_Write(LOG_HEATER_START, d); }
                PrintDebug("[HOST] Auto reflow started\r\n");
                host_send("REFLOW_STARTED");
                g_state = HOST_REFLOW;
            } else {
                g_state = HOST_DEBUG;
            }
            break;

        case HOST_REFLOW:
            /* 等待回流焊完成，只处理加热台状态 */
            {
                HeaterStatus_t hs = Heater_GetCurrentStatus();
                if (hs.state == HEATER_STATE_COMPLETE || hs.state == HEATER_STATE_IDLE) {
                    { uint8_t d[8] = {0}; d[0] = hs.state; Log_Write(LOG_HEATER_DONE, d); }
                    PrintDebug("[HOST] Reflow complete\r\n");
                    host_send("REFLOW_DONE");
                    g_state = HOST_DEBUG;
                } else if (hs.state == HEATER_STATE_ERROR) {
                    { uint8_t d[8] = {0}; d[0] = HEATER_STATE_ERROR; Log_Write(LOG_HEATER_DONE, d); }
                    PrintDebug("[HOST] Reflow ERROR!\r\n");
                    host_send("REFLOW_ERROR");
                    g_state = HOST_ERROR;
                }
            }
            osDelay(200);  /* 200ms 轮询 */
            break;

        case HOST_WAIT_REFILL:
            /* 等待上位机 RESUME */
            osDelay(200);
            break;

        case HOST_ERROR:
            Heater_SendStop();  /* 无论何种错误都停止加热 */
            z_safe();
            { uint8_t d[8] = {0}; d[0] = 1; d[1] = (uint8_t)(g_comp_index>>8); d[2] = (uint8_t)(g_comp_index); Log_Write(LOG_PNP_ERROR, d); }
            PrintDebug("[HOST] ERROR state. Resetting to DEBUG.\r\n");
            g_comp_count = 0;
            g_mark_count = 0;
            g_comp_index = 0;
            osDelay(500);
            g_state = HOST_DEBUG;
            break;

        default:
            break;
        }

        /* ---- 电机错误检测 ---- */
        if (g_motor_error) {
            g_motor_error = false;
            if (g_state != HOST_DEBUG && g_state != HOST_HOME) {
                { uint8_t d[8] = {0}; d[0] = (uint8_t)g_motor_error_detail; Log_Write(LOG_MOTOR_ERROR, d); }
                                if (g_motor_error_detail == MOTOR_ERR_TIMEOUT) {
                    PrintDebug("[HOST] Motor TIMEOUT! Entering ERROR state.\r\n");
                } else {
                    PrintDebug("[HOST] Motor LIMIT/BLOCK! Entering ERROR state.\r\n");
                }
                z_safe();
                g_state = HOST_ERROR;
            }
        }
    }
}