/* ================================================================
 *  app_host.c
 *  分区: §1常量 §2CSV §3辅助 §4调试命令 §5PnP step §6主循环
 * ================================================================ */

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
#include "app_touchgfx_bridge.h"
#include "app_screen_test.h"

extern TIM_HandleTypeDef htim2;  /* Z轴舵机 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* 内部辅助函数前向声明 */
static void start_p1_find_first(void);

/* ================================================================
 *  常量
 * ================================================================ */
#define DEBUG_SPEED     200            /* 调试模式离散移动速度 */
#define DEBUG_ACC       25             /* 调试模式加速度 */
#define JOG_SPEED        200            /* 连续移动速度 (RPM) */
#define JOG_ACC          25             /* 连续移动加速度 */
#define JOG_MMS_TO_RPM  12.0f   /* mm/s → RPM: STEPS_PER_MM/16384*60 */
#define PNP_SPEED_FAST   200   /* 长途移动 */
#define PICK_DELAY_MS    800   /* R轴停稳 + 真空建立前余量 */
#define PLACE_DELAY_MS   300
#define PUMP_BLOW_MS    1000          /* 关气泵后电磁阀吹气时长(ms) */

#define MARK_VERIFY_ERR_MM  0.3f   /* Mark3 验证允许误差 (mm) */
#define P2_SCAN_STEP_MM      5.0f   /* P2 横向步进宽度 (mm) */
#define P2_SCAN_SPEED        14     /* P2 连续扫描速度 (RPM), Cam 需要低速识别 */
#define P2_SCAN_ACC          15     /* P2 连续扫描加速度 */
#define P2_SCAN_TIMEOUT       300   /* P2 非扫描模式超时 (~3s, 对齐/跳转等待) */
#define P2_SCAN_COL_PAD_MS   500    /* 每列额外延时 (ms), 补偿加减速段 */
#define P2_SCAN_POS_UPDATE_MS 100   /* 位置估算 Coord 更新间隔 (ms) */
#define P2_MAX_ALIGN_ITER       5   /* P2 对齐迭代上限，防死循环 */

/* speedModeRun 方向位: 0=CCW→电机x增大(上), 1=CW→电机x减小(下). 方向反了就交换 */
#define P2_SCAN_DIR_UP        1
#define P2_SCAN_DIR_DOWN      0
#define Z_SERVO_CH       2            /* 舵机通道号 */

/* ================================================================
 *  任务内全局状态
 * ================================================================ */
static HostState_t  g_state = HOST_HOME;
static uint8_t     s_bridge_done_notified = 0;  /* Bridge: HOST_DONE 通知已发送标志 */
static bool        s_pnp_phase_logged = false;    /* PnP 阶段日志已发送标志 */
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


/* 标定数据 (Flash 持久化) */
CalibrationData_t g_calib;

/* SET_CAM_OFFSET 两步标定中间态 */
static int32_t g_cam_ref_x = 0, g_cam_ref_y = 0;
static bool    g_cam_ref_valid = false;

/* JOG 状态 */
static bool g_during_cmd = false;  /* 正在执行命令时置位，用于屏蔽回显 */
static bool g_jog_active = false;
static bool g_auto_heat = false;
static bool g_light_on    = false;  /* 下补光灯状态 */
static bool     g_error_entered   = false;
static uint32_t g_error_start_tick = 0;  /* ERROR 30s 超时计时器 */
/* 命令去重：连续两次相同 cmd+param 直接丢弃（中间有别的命令会复位） */
static HostCmd_t    g_last_cmd = HCMD_NONE;
static float        g_last_param = 0.0f;

/* 行解析器 */
static LineParser_t g_parser;

/* Mark 对齐累计偏移 */
static int32_t g_mark_offsets[P2_MARK_COUNT][2];  /* 3个Mark的(dx, dy) 电机步数 */

static int32_t g_mark_count_done = 0;

/* Mark 对齐平均偏移 (mm*10000)，PLACE 时应用到贴装坐标 */
static int32_t g_mark_avg_dx = 0;
static int32_t g_mark_avg_dy = 0;
static int     g_p3_nozzle_retry = 0; /* P3 吸嘴空取重试计数 */
static int     g_p1_retry_count = 0;  /* P1重试计数 */
static int     g_consecutive_failures = 0;  /* 连续元件失败计数 */

/* PCB 坐标系 (P2 建系结果) */
PCBFrame_t g_pcb_frame = {0};

/* Mark 实际机器坐标 (步数)，建系时填入 */
static int32_t g_marks_actual[P2_MARK_COUNT][2];

/* P3 下相机偏移累积 (步数) */
static int32_t g_p3_offset_x = 0;
static int32_t g_p3_offset_y = 0;

/* P4 下相机基线/校验坐标 (步数) */
static int32_t g_p4_base_x = 0;
static int32_t g_p4_base_y = 0;

/* P2 引导式扫描状态 */
static bool    g_mark_scanning = false;
static int32_t g_scan_cols = 0;          /* 扫描总列数 (cols) */
static int32_t g_scan_rows = 0;          /* (保留兼容: rows) */
static int32_t g_scan_cur  = 0;          /* (保留兼容: VISION_ERROR 回退用) */
static uint32_t g_busy_enter_tick = 0;
static bool     g_in_busy = false;
static bool    g_mark_just_jumped = false;  /* 防止 Mark 跳转后冗余二次跳转 */
static int     g_p2_pos_iter = 0;           /* P2 对齐迭代计数，防死循环 */

/* P2 连续扫描状态 */
static bool     g_p2_scanning = false;      /* 是否正在连续扫描 */
static int32_t  g_p2_col = 0;               /* 当前列号 (0..g_scan_cols-1) */
static int32_t  g_p2_col_start_x = 0;       /* 当前列起点 X1 电机坐标 (步数) */
static uint32_t g_p2_col_start_tick = 0;    /* 当前列开始时刻 (tick) */
static uint32_t g_p2_col_time_ticks = 0;    /* 当前列预估总时长 (tick) */
static uint32_t g_p2_last_pos_update = 0;   /* 上次位置估算更新时刻 (tick) */
static int32_t  g_p2_enc_start_x1 = 0;       /* 列起点 X1 31H 编码器值 */
static int32_t  g_p2_enc_start_x2 = 0;       /* 列起点 X2 31H 编码器值 */

/* 散料区子扫描位: [cell][subpos][x/y] */
int32_t g_scatter_subpos[SCATTER_CELLS][SCATTER_SUBPOS][2];
static int     g_p1_scan_pos = 0;   /* P1 当前子扫描位 0..4 */
static int     g_p1_found_pos  = -1;  /* P1 实际找到元件的子位置，-1=无效 */

#define P1_SUBPOS_TIMEOUT_MS  10000 /* P1 单个子位置超时 10s */
static uint32_t g_p1_subpos_start_tick = 0;  /* P1 当前子位置起始 tick */

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
void scatter_init_cells(void) {
    int32_t sx = g_calib.scatter_x_steps;
    int32_t sy = g_calib.scatter_y_steps;
    int32_t size = g_calib.scatter_size_steps;
    /* 单元格中心偏移: ±size/4 */
    int32_t co[4][2] = {
        {+size/4, +size/4},  /* cell 0: motor X+Y+ → host 左上 */
        {-size/4, +size/4},  /* cell 1: motor X-Y+ → host 右上 */
        {+size/4, -size/4},  /* cell 2: motor X+Y- → host 左下 */
        {-size/4, -size/4},  /* cell 3: motor X-Y- → host 右下 */
    };
    /* 子位偏移 (相对格中心): 中心 → 左上 → 右上 → 右下 → 左下 顺时针 */
    int32_t so[5][2] = {
        {       0,        0},  /* 0: 中心 */
        {-size/8, -size/8},  /* 1 */
        {+size/8, -size/8},  /* 2 */
        {+size/8, +size/8},  /* 3 */
        {-size/8, +size/8},  /* 4: 顺时针 */
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
        if ((len >= 12 && memcmp(line, "\"Designator\"", 12) == 0) || (len >= 10 && memcmp(line, "Designator", 10) == 0)) {
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

    char tmp[48];

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
    s_bridge_done_notified = 0;
    s_pnp_phase_logged = false;
    Bridge_NotifyDownloadStatus(0);
    Bridge_NotifySMTStatus(1);
    Bridge_NotifySMTProgress(0, (uint8_t)g_comp_count);
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
        g_state = HOST_P4_BASELINE;

        safe_move_to(g_calib.bottom_cam_x_steps, g_calib.bottom_cam_y_steps, PNP_SPEED, PNP_ACC);
        LowerCam_Light_On();
        Vision_Start(VCMD_P4, 0);
        PrintDebug("[HOST] P4 baseline: moving to bottom cam...\r\n");
    } else if (g_comp_count > 0) {
        PrintDebug("[HOST] No marks, starting find component (P1)...\r\n");
        start_p1_find_first();
    } else {
        host_send("DOWNLOAD_READY");
        g_state = HOST_DEBUG;
    }
}

/* ================================================================
 *  P2 建系启动辅助函数
 * ================================================================ */
static void start_p2_mark_align(void)
{
    g_state = HOST_MARK_ALIGN;

    int32_t scan_step = (int32_t)(P2_SCAN_STEP_MM * STEPS_PER_MM);
    int32_t area_w = g_calib.heat_platform_x_max - g_calib.heat_platform_x_min;
    int32_t area_h = g_calib.heat_platform_y_max - g_calib.heat_platform_y_min;
    if (area_w < 0) area_w = -area_w;
    if (area_h < 0) area_h = -area_h;
    if (area_w > 0 && area_h > 0) {
        g_scan_cols = (area_w + scan_step - 1) / scan_step;
        g_scan_rows = (area_h + scan_step - 1) / scan_step;
        if (g_scan_cols < 1) g_scan_cols = 1;
        if (g_scan_rows < 1) g_scan_rows = 1;
        g_scan_cur = 0;
        g_in_busy = false;
        g_mark_scanning = true;
        g_p2_scanning = true;
        g_p2_col = 0;
        g_p2_last_pos_update = 0;
        safe_move_to(g_calib.heat_platform_x_min + g_calib.cam_to_nozzle_dx_steps,
                     g_calib.heat_platform_y_min + g_calib.cam_to_nozzle_dy_steps,
                     PNP_SPEED, PNP_ACC);
        PrintDebug("[HOST] P2 scan: %ld cols, step=%ld steps, speed=%dRPM\r\n",
                   (long)g_scan_cols, (long)scan_step, P2_SCAN_SPEED);
    } else {
        g_mark_scanning = false;
        g_p2_scanning = false;
        PrintDebug("[HOST] PCB area uncalibrated, single-spot P2.\r\n");
    }

    motorSyncEnable(1);
    osDelay(50);

    Vision_Start(VCMD_P2, 0);
    Bridge_NotifyLog(5, (uint8_t)g_mark_count);
    PrintDebug("[HOST] Starting Mark alignment (P2, %u marks)...\r\n", g_mark_count);
}

/* ================================================================
 *  P1 找首个元件辅助函数
 * ================================================================ */
static void start_p1_find_first(void)
{
    if (g_comp_count == 0) {
        host_send("DOWNLOAD_READY");
        g_state = HOST_DEBUG;
        return;
    }

    g_comp_index = 0;
    int cl = component_cell(&g_components[0]);
    g_p1_scan_pos = 0;
    g_p1_found_pos  = -1;
    g_p1_subpos_start_tick = osKernelGetTickCount();
    safe_move_to(g_scatter_subpos[cl][0][0] + g_calib.cam_to_nozzle_dx_steps,
                 g_scatter_subpos[cl][0][1] + g_calib.cam_to_nozzle_dy_steps,
                 PNP_SPEED, PNP_ACC);
    Vision_Start(VCMD_P1, footprint_to_class_id(g_components[0].footprint));
    g_state = HOST_FIND_COMP;
    PrintDebug("[HOST] Starting find component (P1)...\r\n");
}

/* ================================================================
 *  调试模式命令处理
 * ================================================================ */
/* ---- 标定命令处理 ---- */
static bool handle_calib_cmd(HostParsed_t *cmd) {
    switch (cmd->cmd) {
    case HCMD_SET_SCATTER_AREA:
        g_calib.scatter_x_steps = Coord_Get().x;
        g_calib.scatter_y_steps = Coord_Get().y;
        PrintDebug("[HOST] SET_SCATTER_AREA: (%ld,%ld)\r\n", (long)(-(int32_t)Coord_Get().y), (long)Coord_Get().x);
        return true;
    case HCMD_SET_SCATTER_SIZE:
        g_calib.scatter_size_steps = (int32_t)(cmd->param * STEPS_PER_MM);
        PrintDebug("[HOST] SET_SCATTER_SIZE: %.1fmm -> %ld steps\r\n", cmd->param, (long)g_calib.scatter_size_steps);
        return true;
    case HCMD_SET_HEATER_PLATFORM_MIN:
        g_calib.heat_platform_x_min = Coord_Get().x;
        g_calib.heat_platform_y_min = Coord_Get().y;
        PrintDebug("[HOST] SET_HEATER_PLATFORM_MIN: (%ld,%ld)\r\n", (long)(-(int32_t)Coord_Get().y), (long)Coord_Get().x);
        return true;
    case HCMD_SET_HEATER_PLATFORM_MAX:
        g_calib.heat_platform_x_max = Coord_Get().x;
        g_calib.heat_platform_y_max = Coord_Get().y;
        PrintDebug("[HOST] SET_HEATER_PLATFORM_MAX: (%ld,%ld)\r\n", (long)(-(int32_t)Coord_Get().y), (long)Coord_Get().x);
        return true;
    case HCMD_SET_BOTTOM_CAM:
        g_calib.bottom_cam_x_steps = Coord_Get().x;
        g_calib.bottom_cam_y_steps = Coord_Get().y;
        PrintDebug("[HOST] SET_BOTTOM_CAM: (%ld,%ld)\r\n", (long)(-(int32_t)Coord_Get().y), (long)Coord_Get().x);
        return true;
    case HCMD_SET_Z_SAFE:
        { float ang = Servo_GetAngle(Z_SERVO_CH); if (ang >= 0.0f) g_calib.z_safe_angle = ang;
          PrintDebug("[HOST] SET_Z_SAFE: %.1f deg\r\n", g_calib.z_safe_angle); }
        return true;
    case HCMD_SET_Z_PICK:
        { float ang = Servo_GetAngle(Z_SERVO_CH); if (ang >= 0.0f) g_calib.z_pick_angle = ang;
          PrintDebug("[HOST] SET_Z_PICK: %.1f deg\r\n", g_calib.z_pick_angle); }
        return true;
    case HCMD_SET_Z_PLACE:
        { float ang = Servo_GetAngle(Z_SERVO_CH); if (ang >= 0.0f) g_calib.z_place_angle = ang;
          PrintDebug("[HOST] SET_Z_PLACE: %.1f deg\r\n", g_calib.z_place_angle); }
        return true;
    case HCMD_SET_R_ZERO:
        r_axis_set_zero();
        PrintDebug("[HOST] SET_R_ZERO: R axis zeroed\r\n");
        return true;
    case HCMD_R_CALIB:
        { float a = r_axis_calibrate(); if (a > 0) PrintDebug("[HOST] R_CALIB done: A=%.2f\r\n", (double)a); }
        return true;
    case HCMD_SET_CAM_OFFSET:
        if (!g_cam_ref_valid) {
            g_cam_ref_x = Coord_Get().x;
            g_cam_ref_y = Coord_Get().y;
            g_cam_ref_valid = true;
            PrintDebug("[HOST] SET_CAM_OFFSET step1: nozzle at (%ld,%ld). Move camera to same mark, repeat.\r\n",
                       (long)g_cam_ref_x, (long)g_cam_ref_y);
        } else {
            g_calib.cam_to_nozzle_dx_steps = Coord_Get().x - g_cam_ref_x;
            g_calib.cam_to_nozzle_dy_steps = Coord_Get().y - g_cam_ref_y;
            g_cam_ref_valid = false;
            PrintDebug("[HOST] SET_CAM_OFFSET: offset=(%ld,%ld) steps\r\n",
                       (long)g_calib.cam_to_nozzle_dx_steps, (long)g_calib.cam_to_nozzle_dy_steps);
        }
        return true;
    case HCMD_SAVE_CALIB:
        if (Calib_Save(&g_calib) == 0) { PrintDebug("[HOST] SAVE_CALIB: saved.\r\n"); host_send("CALIB_SAVED"); }
        else { PrintDebug("[HOST] SAVE_CALIB: FAILED!\r\n"); host_send("CALIB_SAVE_FAILED"); }
        return true;
    case HCMD_RESTORE_CALIB:
        g_calib.scatter_x_steps = 58574;
        g_calib.scatter_y_steps = -25753;
        g_calib.scatter_size_steps = 37888;
        g_calib.heat_platform_x_min = 24782;
        g_calib.heat_platform_y_min = -84633;
        g_calib.heat_platform_x_max = 70862;
        g_calib.heat_platform_y_max = -130713;
        g_calib.bottom_cam_x_steps = -203;
        g_calib.bottom_cam_y_steps = -25753;
        g_calib.cam_to_nozzle_dx_steps = -5735;
        g_calib.cam_to_nozzle_dy_steps = -25035;
        g_calib.z_safe_angle = 74.9f;
        g_calib.z_pick_angle = 115.9f;
        g_calib.z_place_angle = 115.9f;
        g_calib.cam_p1_val_to_steps = 0.0f;
        g_calib.cam_p3_val_to_steps = 0.0f;
        scatter_init_cells();
        if (Calib_Save(&g_calib) == 0) { PrintDebug("[HOST] RESTORE_CALIB: saved.\r\n"); host_send("RESTORE_CALIB_OK"); }
        else { PrintDebug("[HOST] RESTORE_CALIB: FAILED!\r\n"); host_send("RESTORE_CALIB_FAILED"); }
        return true;
    default: return false;
    }
}

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

    if (handle_calib_cmd(cmd)) { g_during_cmd = false; return; }

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
        move_set_pad_ms(500);
        int ret = safe_move_to(Coord_Get().x + dx, Coord_Get().y + dy, DEBUG_SPEED, DEBUG_ACC);
        move_set_pad_ms(3000);
        g_jog_active = false;
        /* 坐标显示转换：电机坐标系→上位机坐标系
           host_x = -motor_y (Y电机+ = 左 = host X-), host_y = +motor_x (X电机+ = 上 = host Y+) */
        if (ret < 0) {
            PrintDebug("[HOST] %s %.1fmm INTERRUPTED(ret=%d) pos=(%ld,%ld)\r\n",
                       tbl[idx].name, cmd->param, ret, (long)(-(int32_t)Coord_Get().y), (long)Coord_Get().x);
        } else {
            PrintDebug("[HOST] %s %.1fmm -> (%ld,%ld)\r\n",
                       tbl[idx].name, cmd->param, (long)(-(int32_t)Coord_Get().y), (long)Coord_Get().x);
        }
        break;
    }

    case HCMD_MOVE_UP_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        z_safe();
        osDelay(100);
        positionMode3Run(X1_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, JOG_MAX_STEPS);
        positionMode3Run(X2_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG UP %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_DOWN_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        z_safe();
        osDelay(100);
        positionMode3Run(X1_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, -JOG_MAX_STEPS);
        positionMode3Run(X2_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, -JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG DOWN %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_LEFT_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        z_safe();
        osDelay(100);
        positionMode3Run(Y_ADDR, (uint16_t)(cmd->param * JOG_MMS_TO_RPM), JOG_ACC, JOG_MAX_STEPS);
        motorSyncTrigger(0);
        PrintDebug("[HOST] JOG LEFT %.1f\r\n", cmd->param);
        break;

    case HCMD_MOVE_RIGHT_START:
        if (g_jog_active) disable_sync_stop();
        g_jog_active = true;
        z_safe();
        osDelay(100);
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
        int32_t tx = (int32_t)(cmd->param2 * STEPS_PER_MM);
        int32_t ty = (int32_t)(-cmd->param * STEPS_PER_MM);
        if (g_jog_active) { disable_sync_stop(); g_jog_active = false; }
        safe_move_to(tx, ty, DEBUG_SPEED, DEBUG_ACC);
        /* 坐标显示转换：电机坐标系→上位机坐标系
           host_x = -motor_y, host_y = +motor_x */
        PrintDebug("[HOST] MOVE_TO (%.1f,%.1f)→(%ld,%ld)\r\n",
                   cmd->param, cmd->param2, (long)(-(int32_t)Coord_Get().y), (long)Coord_Get().x);
        break;
    }

    case HCMD_SET_ORIGIN:
        if (g_state != HOST_HOME && g_state != HOST_DEBUG) {
            PrintDebug("[HOST] SET_ORIGIN ignored: not in HOME/DEBUG\r\n");
            break;
        }
        motorSetZero(X1_ADDR);
        motorSetZero(X2_ADDR);
        motorSetZero(Y_ADDR);
        Coord_SetHome();
        osDelay(100);
        PrintDebug("[HOST] SET_ORIGIN\r\n");
        if (g_state == HOST_HOME) {
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
        /* R 轴旋转角度 (0~360), 非阻塞启动 */
        {
            float angle = cmd->param;
            if (angle < 0.0f) angle = 0.0f;
            if (angle > 360.0f) angle = 360.0f;
            r_axis_start(angle, R_SPEED_RPM);
            PrintDebug("[HOST] SET_R_AXIS %.1f deg (started)\r\n", angle);
        }
        break;

    case HCMD_MSCNT_TEST:
        MSCNT_Test();
        PrintDebug("[HOST] MSCNT test done\r\n");
        break;

    case HCMD_PUMP_ON:
        Pump_On();
        PrintDebug("[HOST] PUMP_ON\r\n");
        break;

    case HCMD_PUMP_OFF:
        Pump_Off();
        PrintDebug("[HOST] PUMP_OFF\r\n");
        break;

    case HCMD_HEAT_ON:
        if (Heater_SendStart()) {
            PrintDebug("[HOST] HEAT_ON\r\n");
        } else {
            PrintDebug("[HOST] HEAT_ON TX FAILED\r\n");
        }
        break;

    case HCMD_HEAT_OFF:
        Heater_SendStop();
        PrintDebug("[HOST] HEAT_OFF\r\n");
        break;

    case HCMD_HEATER_QUERY:
        Heater_SendQuery();
        PrintDebug("[HOST] HEATER_QUERY\r\n");
        break;

    case HCMD_HOLD_TEMP:
        if (Heater_SendHold()) {
            PrintDebug("[HOST] HOLD_TEMP\r\n");
            host_send("HOLD_TEMP_OK");
        } else {
            PrintDebug("[HOST] HOLD_TEMP TX FAILED\r\n");
            host_send("HOLD_TEMP_FAILED");
        }
        break;

    case HCMD_EXIT_DEBUG:
        g_state = HOST_DEBUG;
        g_jog_active = false;
        PrintDebug("[HOST] Exit debug, back to IDLE.\r\n");
        host_send("EXIT_DEBUG_MODE");
        osDelay(50);
        host_send("DOWNLOAD_READY");
        break;

    case HCMD_RESUME:
        if (g_state == HOST_WAIT_REFILL || g_state == HOST_ERROR) {
            if (g_state == HOST_ERROR && g_comp_count == 0) {
                PrintDebug("[HOST] RESUME from ERROR but no component data, going DEBUG.\r\n");
                g_error_entered = false;
                g_state = HOST_DEBUG;
                break;
            }
            g_consecutive_failures = 0; g_p1_scan_pos = 0; g_error_entered = false;
            g_p1_found_pos  = -1;
            g_p1_subpos_start_tick = osKernelGetTickCount();
            Component_t *rc = &g_components[g_comp_index];
            int cl = component_cell(rc);
            safe_move_to(g_scatter_subpos[cl][0][0] + g_calib.cam_to_nozzle_dx_steps, g_scatter_subpos[cl][0][1] + g_calib.cam_to_nozzle_dy_steps,
                         PNP_SPEED_FAST, PNP_ACC);
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
        Coord_Invalidate();
        nozzle_off();
        Valve_On(); osDelay(200); Valve_Off();
        z_safe();
        g_error_entered = false;
        g_state = HOST_DEBUG;
        g_comp_count = 0;
        g_mark_count = 0;
        g_comp_index = 0;
        g_p3_nozzle_retry = 0;
        PrintDebug("[HOST] ABORT: motion stopped, back to DEBUG\r\n");
        Heater_SendStop();  /* 若回流焊进行中也停止 */
        { uint8_t d[8] = {0}; Log_Write(LOG_ABORT, d); }
        host_send("ABORT_OK");
        break;

    case HCMD_HOME:
        if (g_jog_active) { disable_sync_stop(); g_jog_active = false; }
        safe_move_to(0, 0, DEBUG_SPEED, DEBUG_ACC);
        PrintDebug("[HOST] HOME -> (0,0)\r\n");
        break;

    case HCMD_VALVE_ON:
        Valve_On();
        PrintDebug("[HOST] VALVE_ON\r\n");
        break;

    case HCMD_VALVE_OFF:
        Valve_Off();
        PrintDebug("[HOST] VALVE_OFF\r\n");
        break;

    case HCMD_LIGHT:
        g_light_on = !g_light_on;
        if (g_light_on) { LowerCam_Light_On();  host_send("LIGHT_ON");  PrintDebug("[HOST] LIGHT: ON\r\n"); }
        else            { LowerCam_Light_Off(); host_send("LIGHT_OFF"); PrintDebug("[HOST] LIGHT: OFF\r\n"); }
        break;

    case HCMD_CALIB_ENC: {
        int32_t enc0_x1, enc0_x2, enc1_x1, enc1_x2;
        g_enc_ready[X1_ADDR]=false; readRealTimeLocation(X1_ADDR);
        {uint32_t _t=osKernelGetTickCount(); while(!g_enc_ready[X1_ADDR]&&(osKernelGetTickCount()-_t)<100){osDelay(2);}}
        enc0_x1=g_enc_ready[X1_ADDR]?g_enc_pos[X1_ADDR]:0;
        g_enc_ready[X2_ADDR]=false; readRealTimeLocation(X2_ADDR);
        {uint32_t _t=osKernelGetTickCount(); while(!g_enc_ready[X2_ADDR]&&(osKernelGetTickCount()-_t)<100){osDelay(2);}}
        enc0_x2=g_enc_ready[X2_ADDR]?g_enc_pos[X2_ADDR]:0;
        g_axes_done_bits=0; g_axes_error=false;
        motorSyncEnable(1); osDelay(5);
        positionMode2Run(X1_ADDR,100,50,10000); positionMode2Run(X2_ADDR,100,50,10000);
        motorSyncTrigger(0); osDelay(2000);
        g_enc_ready[X1_ADDR]=false; readRealTimeLocation(X1_ADDR);
        {uint32_t _t=osKernelGetTickCount(); while(!g_enc_ready[X1_ADDR]&&(osKernelGetTickCount()-_t)<100){osDelay(2);}}
        enc1_x1=g_enc_ready[X1_ADDR]?g_enc_pos[X1_ADDR]:0;
        g_enc_ready[X2_ADDR]=false; readRealTimeLocation(X2_ADDR);
        {uint32_t _t=osKernelGetTickCount(); while(!g_enc_ready[X2_ADDR]&&(osKernelGetTickCount()-_t)<100){osDelay(2);}}
        enc1_x2=g_enc_ready[X2_ADDR]?g_enc_pos[X2_ADDR]:0;
        int32_t d1=enc1_x1-enc0_x1,d2=enc1_x2-enc0_x2,avg=(d1+d2)/2;
        PrintDebug("[CALIB] enc0=(%ld,%ld) enc1=(%ld,%ld) dx=(%ld,%ld) avg=%ld\r\n",(long)enc0_x1,(long)enc0_x2,(long)enc1_x1,(long)enc1_x2,(long)d1,(long)d2,(long)avg);
        PrintDebug("[CALIB] P2_ENC_RATIO: #define P2_ENC_RATIO_NUM 10000\r\n");
        PrintDebug("[CALIB]               #define P2_ENC_RATIO_DEN %ld\r\n",(long)avg);
        break;
    }


    case HCMD_SCREEN_TEST:
        osThreadNew(StartScreenTestTask, NULL, &screenTestTask_attributes);
        host_send("SCREEN_TEST_STARTED");
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



    switch (vs) {

    case VISION_RDY:
        /* Cam responded rdy after p2, send go */
        g_in_busy = false;
        g_p2_pos_iter = 0;
        Vision_Go();
        break;

    /* ---- 连续扫描 / 跳转搜索状态机 ---- */
    case VISION_BUSY:
        if (!g_in_busy) {
            g_in_busy = true;
            g_busy_enter_tick = osKernelGetTickCount();

            if (g_p2_scanning) {
                /* 新列启动: X1+X2 速度模式连续扫描 */
                g_p2_col_start_x = Coord_Get().x;
                g_p2_col_start_tick = osKernelGetTickCount();
                g_p2_last_pos_update = g_p2_col_start_tick;

                /* 读取 31H 编码器基准值 (连读两次, 第一次冲刷可能残留的旧响应) */
                g_enc_ready[X1_ADDR] = false; readRealTimeLocation(X1_ADDR);
                { uint32_t _t0 = osKernelGetTickCount();
                  while (!g_enc_ready[X1_ADDR] && (osKernelGetTickCount() - _t0) < 100) { osDelay(2); } }
                /* 第二次读: 确保拿到列起点真值 */
                g_enc_ready[X1_ADDR] = false; readRealTimeLocation(X1_ADDR);
                { uint32_t _t0 = osKernelGetTickCount();
                  while (!g_enc_ready[X1_ADDR] && (osKernelGetTickCount() - _t0) < 100) { osDelay(2); } }
                g_p2_enc_start_x1 = g_enc_ready[X1_ADDR] ? g_enc_pos[X1_ADDR] : 0;
                g_enc_ready[X2_ADDR] = false; readRealTimeLocation(X2_ADDR);
                { uint32_t _t0 = osKernelGetTickCount();
                  while (!g_enc_ready[X2_ADDR] && (osKernelGetTickCount() - _t0) < 100) { osDelay(2); } }
                g_enc_ready[X2_ADDR] = false; readRealTimeLocation(X2_ADDR);
                { uint32_t _t0 = osKernelGetTickCount();
                  while (!g_enc_ready[X2_ADDR] && (osKernelGetTickCount() - _t0) < 100) { osDelay(2); } }
                g_p2_enc_start_x2 = g_enc_ready[X2_ADDR] ? g_enc_pos[X2_ADDR] : 0;

                uint8_t dir = (g_p2_col & 1) ? P2_SCAN_DIR_DOWN : P2_SCAN_DIR_UP;
                p2_scan_start(dir, P2_SCAN_SPEED, P2_SCAN_ACC);

                /* 预计算列时间: col_h / speed, tick≈ms */
                int32_t col_h = g_calib.heat_platform_x_max - g_calib.heat_platform_x_min;
                if (col_h < 0) col_h = -col_h;
                float spms = (float)P2_SCAN_SPEED * 32768.0f / 60000.0f;
                /* col_h/spms 结果为 ms, 直接与 tick 比较; 依赖 TICK_RATE_HZ=1000 */
                g_p2_col_time_ticks = (uint32_t)((float)col_h / spms) + pdMS_TO_TICKS(P2_SCAN_COL_PAD_MS);

                PrintDebug("[HOST] P2 scan col %ld/%ld dir=%d est=%ldms\r\n",
                           (long)g_p2_col, (long)g_scan_cols, (int)dir,
                           (long)(g_p2_col_time_ticks));
            }
        }

        if (!g_p2_scanning) {
            if ((osKernelGetTickCount() - g_busy_enter_tick) < pdMS_TO_TICKS(P2_SCAN_TIMEOUT * 10)) break;
            g_in_busy = false;
            g_busy_enter_tick = osKernelGetTickCount();
            break;
        }

        {
            uint32_t now = osKernelGetTickCount();
            uint32_t elapsed = now - g_p2_col_start_tick;

            if ((now - g_p2_last_pos_update) >= pdMS_TO_TICKS(P2_SCAN_POS_UPDATE_MS)) {
                g_p2_last_pos_update = now;
                int32_t sign = ((g_p2_col & 1) ? P2_SCAN_DIR_DOWN : P2_SCAN_DIR_UP) == P2_SCAN_DIR_UP ? 1 : -1;
                int32_t est_x = p2_scan_estimate_x(g_p2_col_start_x, sign, P2_SCAN_SPEED, elapsed);
                Coord_UpdateXY(est_x, Coord_Get().y);
            }

            if (elapsed >= g_p2_col_time_ticks) {
                p2_scan_stop();
                {
                    int32_t sign = ((g_p2_col & 1) ? P2_SCAN_DIR_DOWN : P2_SCAN_DIR_UP) == P2_SCAN_DIR_UP ? 1 : -1;
                    int32_t est_x = p2_scan_estimate_x(g_p2_col_start_x, sign, P2_SCAN_SPEED, elapsed);
                    Coord_UpdateXY(est_x, Coord_Get().y);
                }

                g_p2_col++;
                if (g_p2_col >= g_scan_cols) {
                    PrintDebug("[HOST] P2 scan exhausted (%ld cols), Mark0 not found.\r\n",
                               (long)g_scan_cols);
                    g_p2_scanning = false;
                    g_mark_scanning = false;
                    Vision_ForceIdle();
                    g_state = HOST_ERROR;
                    break;
                }

                {
                    int32_t y_dir_sign = (g_calib.heat_platform_y_max >= g_calib.heat_platform_y_min) ? 1 : -1;
                    int32_t step = (int32_t)(P2_SCAN_STEP_MM * STEPS_PER_MM);
                    p2_scan_step_y(y_dir_sign * step, PNP_SPEED, PNP_ACC);
                }
                g_in_busy = false;
                PrintDebug("[HOST] P2 scan col %ld done, stepping to col %ld\r\n",
                           (long)(g_p2_col - 1), (long)g_p2_col);
            }
        }
        break;

    case VISION_GOT_STOP:
        if (g_p2_scanning) {
            g_p2_scanning = false;
            int32_t real_x = p2_stop_and_read_pos(g_p2_enc_start_x1, g_p2_enc_start_x2, g_p2_col_start_x);
            if (real_x >= 0) {
                Coord_UpdateXY(real_x, Coord_Get().y);
                PrintDebug("[HOST] P2 stop col %ld real_x=%ld\r\n", (long)g_p2_col, (long)real_x);
            } else {
                /* 31H read failed, fallback to estimate */
                uint32_t elapsed = osKernelGetTickCount() - g_p2_col_start_tick;
                int32_t sign = ((g_p2_col & 1) ? P2_SCAN_DIR_DOWN : P2_SCAN_DIR_UP) == P2_SCAN_DIR_UP ? 1 : -1;
                int32_t est_x = p2_scan_estimate_x(g_p2_col_start_x, sign, P2_SCAN_SPEED, elapsed);
                Coord_UpdateXY(est_x, Coord_Get().y);
                PrintDebug("[HOST] P2 stop col %ld est_x=%ld (31H FAIL)\r\n", (long)g_p2_col, (long)est_x);
            }
        }
        g_mark_scanning = false;
        g_in_busy = false;
        g_p2_pos_iter = 0;
        {
            int32_t idx = r->mark_index;
            if (idx >= 1 && idx < P2_MARK_COUNT && !g_mark_just_jumped) {
                float tdx = g_marks[idx].target_x - g_marks[idx-1].target_x;
                float tdy = g_marks[idx].target_y - g_marks[idx-1].target_y;
                int32_t dx = (int32_t)(tdy * STEPS_PER_MM);
                int32_t dy = -(int32_t)(tdx * STEPS_PER_MM);
                int32_t prev = idx - 1;
                safe_move_to(g_marks_actual[prev][0] + dx + g_calib.cam_to_nozzle_dx_steps,
                             g_marks_actual[prev][1] + dy + g_calib.cam_to_nozzle_dy_steps,
                             PNP_SPEED, PNP_ACC);
                g_mark_just_jumped = true;
                PrintDebug("[HOST] P2 jump Mark%ld: theory(%.1f,%.1f)mm → (%ld,%ld)\r\n",
                           (long)idx, tdx, tdy, (long)Coord_Get().x, (long)Coord_Get().y);
            }
        }
        Vision_Go();
        break;
    /* ---- 收到偏移 → 移动并对齐 (保持不变) ---- */
    case VISION_GOT_POS: {
        g_mark_scanning = false;       /* 收到 pos 数据，停止网格扫描 */
        g_mark_just_jumped = false;   /* 进入对齐阶段，清除跳转标志 */
        int32_t idx = r->mark_index;

        /* P2 对齐迭代上限：防 Cam 端不收敛导致死循环 */
        g_p2_pos_iter++;
        if (g_p2_pos_iter > P2_MAX_ALIGN_ITER) {
            PrintDebug("[HOST] Mark%ld P2 align max iter (%d) exceeded\r\n",
                       (long)idx, P2_MAX_ALIGN_ITER);
            Vision_ForceIdle();
            g_state = HOST_ERROR;
            break;
        }

        if (idx >= 0 && idx < P2_MARK_COUNT) {
            if (r->dx != 0 || r->dy != 0) {
                int32_t dx_s = -(int32_t)(r->dy);  // cam Y → X1+X2, value already in motor steps
                int32_t dy_s = -(int32_t)(r->dx);  // cam X → Y 电机, value already in motor steps
                safe_move_to(Coord_Get().x + dx_s, Coord_Get().y + dy_s, 100, 25);
                PrintDebug("[HOST] Mark%ld offset: (%ld,%ld)px → move(%ld,%ld)steps\r\n",
                           (long)idx, (long)r->dx, (long)r->dy, (long)dx_s, (long)dy_s);
            }
            /* Mark真实坐标 = 摄像头位置 = 吸嘴 - cam_to_nozzle (cam_to_nozzle为负值,减负得正) */
            g_marks_actual[idx][0] = Coord_Get().x - g_calib.cam_to_nozzle_dx_steps;
            g_marks_actual[idx][1] = Coord_Get().y - g_calib.cam_to_nozzle_dy_steps;
            g_mark_offsets[idx][0] = r->dx;
            g_mark_offsets[idx][1] = r->dy;
            g_mark_count_done = idx + 1;
        }
        Vision_Go();
        vTaskDelay(pdMS_TO_TICKS(80));
        break;
    }

    /* ---- 建系 (保持不变) ---- */
    case VISION_DONE: {
        g_mark_scanning = false;
        g_p2_scanning = false;
        if (g_mark_count < P2_MARK_COUNT) {
            PrintDebug("[HOST] P2: only %u marks (need %d), aborting.\r\n", g_mark_count, P2_MARK_COUNT);
            Vision_SendEnd();
            Vision_ForceIdle();
            g_state = HOST_ERROR;
            break;
        }
        int32_t a1x = g_marks_actual[0][0], a1y = g_marks_actual[0][1];
        int32_t a2x = g_marks_actual[1][0], a2y = g_marks_actual[1][1];
        int32_t a3x = g_marks_actual[2][0], a3y = g_marks_actual[2][1];

        float t1x = g_marks[0].target_x, t1y = g_marks[0].target_y;
        float t2x = g_marks[1].target_x, t2y = g_marks[1].target_y;
        float t3x = g_marks[2].target_x, t3y = g_marks[2].target_y;

        float theory_ang = atan2f(t2y - t1y, t2x - t1x);

        /* 电机坐标 → 相机坐标: cam_X = -(Y电机), cam_Y = X1+X2 */
        float ac1x = -(float)a1y, ac1y = (float)a1x;
        float ac2x = -(float)a2y, ac2y = (float)a2x;
        float actual_ang = atan2f(ac2y - ac1y, ac2x - ac1x);
        float theta = actual_ang - theory_ang;

        float mt_x = (t1x + t2x) * 0.5f * STEPS_PER_MM;
        float mt_y = (t1y + t2y) * 0.5f * STEPS_PER_MM;
        float ma_x = (ac1x + ac2x) * 0.5f;
        float ma_y = (ac1y + ac2y) * 0.5f;

        float cos_t = cosf(theta), sin_t = sinf(theta);
        float o_cx = ma_x - (mt_x * cos_t - mt_y * sin_t);
        float o_cy = ma_y - (mt_x * sin_t + mt_y * cos_t);
        g_pcb_frame.origin_x_steps = (int32_t)o_cy;    /* cam_Y → X1+X2 */
        g_pcb_frame.origin_y_steps = (int32_t)(-o_cx); /* cam_X → Y电机(取反) */
        g_pcb_frame.rotation_rad = theta;

        float t3x_s = t3x * STEPS_PER_MM;
        float t3y_s = t3y * STEPS_PER_MM;
        float pcx = (t3x_s * cos_t - t3y_s * sin_t) + o_cx;
        float pcy = (t3x_s * sin_t + t3y_s * cos_t) + o_cy;
        int32_t pred_x = (int32_t)pcy;  /* cam_Y → X1+X2 */
        int32_t pred_y = (int32_t)(-pcx); /* cam_X → Y电机(取反) */
        int32_t err_x = pred_x - a3x, err_y = pred_y - a3y;
        float err_mm = sqrtf((float)(err_x*err_x + err_y*err_y)) / STEPS_PER_MM;
        g_pcb_frame.valid = (err_mm < MARK_VERIFY_ERR_MM);

        PrintDebug("[HOST] === PCB Frame ===\r\n");
        PrintDebug("[HOST] origin=(%ld,%ld) theta=%.4frad(%.2fdeg)\r\n",
                   (long)g_pcb_frame.origin_x_steps, (long)g_pcb_frame.origin_y_steps,
                   theta, theta * 57.29578f);
        PrintDebug("[HOST] Mark3 verify: err=%.3fmm %s\r\n", err_mm,
                   g_pcb_frame.valid ? "OK" : "FAIL");

        /* 诊断: 比较 Coord 和 建系推算位置 */
        {
            MachineCoord_t cc = Coord_Get();
            /* 用 frame 反算 Mark2 相机应该在哪 */
            float ltx = g_marks[2].target_x * STEPS_PER_MM;
            float lty = g_marks[2].target_y * STEPS_PER_MM;
            float rcx = ltx * cos_t - lty * sin_t;
            float rcy = ltx * sin_t + lty * cos_t;
            int32_t frame_x = (int32_t)rcy + g_pcb_frame.origin_x_steps + g_calib.cam_to_nozzle_dx_steps;
            int32_t frame_y = (int32_t)(-rcx) + g_pcb_frame.origin_y_steps + g_calib.cam_to_nozzle_dy_steps;
            PrintDebug("[DIAG] After P2: Coord=(%ld,%ld) frame_Mark2_cam=(%ld,%ld) delta=(%ld,%ld)\r\n",
                       (long)cc.x, (long)cc.y, (long)frame_x, (long)frame_y,
                       (long)(cc.x - frame_x), (long)(cc.y - frame_y));
        }

        if (g_mark_count_done >= 3) {
            g_mark_avg_dx = (g_mark_offsets[0][0] + g_mark_offsets[1][0] + g_mark_offsets[2][0]) / 3;
            g_mark_avg_dy = (g_mark_offsets[0][1] + g_mark_offsets[1][1] + g_mark_offsets[2][1]) / 3;
        }

        g_comp_index = 0;
        Vision_SendEnd();
        PrintDebug("[HOST] P2 done, starting P4 verify...\r\n");
        g_state = HOST_P4_VERIFY;
        safe_move_to(g_calib.bottom_cam_x_steps, g_calib.bottom_cam_y_steps, PNP_SPEED, PNP_ACC);
        LowerCam_Light_On();
        Vision_Start(VCMD_P4, 0);
        break;
    }

    case VISION_ERROR:
        PrintDebug("[HOST] Mark alignment ERROR: %s\r\n", Vision_GetError());
        if (g_p2_scanning) {
            p2_scan_stop();
            g_p2_col++;
            if (g_p2_col >= g_scan_cols) {
                g_p2_scanning = false;
                g_mark_scanning = false;
                Vision_SendEnd();
                Vision_ForceIdle();
                g_state = HOST_ERROR;
                break;
            }
            {
                int32_t y_dir_sign = (g_calib.heat_platform_y_max >= g_calib.heat_platform_y_min) ? 1 : -1;
                int32_t step = (int32_t)(P2_SCAN_STEP_MM * STEPS_PER_MM);
                int32_t base_y = g_calib.heat_platform_y_min;
                int32_t target_y = base_y + y_dir_sign * g_p2_col * step;
                safe_move_to(g_calib.heat_platform_x_min + g_calib.cam_to_nozzle_dx_steps,
                             target_y + g_calib.cam_to_nozzle_dy_steps,
                             PNP_SPEED, PNP_ACC);
            }
            g_mark_scanning = true;
            g_p2_scanning = true;
            Vision_BackToSearch();
            g_in_busy = false;
            PrintDebug("[HOST] P2 error, resuming scan from col %ld\r\n", (long)g_p2_col);
        } else {
            g_mark_scanning = false;
            g_p2_scanning = false;
            Vision_SendEnd();
            Vision_ForceIdle();
            g_state = HOST_ERROR;
        }
        break;
    default:
        break;
    }

    if (Vision_IsTimedOut()) {
        PrintDebug("[HOST] Mark align timeout!\r\n");
        Vision_ForceIdle();
        g_state = HOST_ERROR;
        return;
    }
}

/* ---- R 轴角度矫正辅助：从视觉结果提取角度，超过阈值则启动非阻塞旋转 ---- */
bool host_start_r_correction(const VisionResult_t *r, const char *stage) {
    if (!r || !r->angle_valid) return false;
    float ang = (float)r->angle_x100 / 100.0f;
    if (fabsf(ang) <= R_CORRECTION_THRESHOLD_DEG) return false;
    PrintDebug("[HOST] %s: R correction %.2f deg\r\n", stage, (double)ang);
    r_axis_rotate(ang, R_SPEED_RPM);
    return false;  /* 阻塞已完成, 无需 phase 等待 */
}

/* ================================================================
 *  P4 基线/校验步函数
 * ================================================================ */

static void p4_baseline_step(void)
{
    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    if (Vision_IsTimedOut()) {
        LowerCam_Light_Off();
        PrintDebug("[HOST] P4 baseline timeout\r\n");
        Vision_ForceIdle();
        g_state = HOST_ERROR;
        return;
    }

    switch (vs) {
    case VISION_GOT_POS: {
        int32_t dx_s = (int32_t)(r->dy);
        int32_t dy_s = -(int32_t)(r->dx);
        if (dx_s != 0 || dy_s != 0) {
            safe_move_to(Coord_Get().x + dx_s, Coord_Get().y + dy_s, PNP_SPEED, PNP_ACC);
        }
        Vision_Go();
        break;
    }

    case VISION_DONE:
        g_p4_base_x = Coord_Get().x;
        g_p4_base_y = Coord_Get().y;
        LowerCam_Light_Off();
        Vision_SendEnd();
        PrintDebug("[HOST] P4 baseline recorded: (%ld,%ld)\r\n", (long)g_p4_base_x, (long)g_p4_base_y);
        start_p2_mark_align();
        break;

    case VISION_ERROR:
        LowerCam_Light_Off();
        PrintDebug("[HOST] P4 baseline ERROR: %s\r\n", Vision_GetError());
        Vision_ForceIdle();
        g_state = HOST_ERROR;
        break;

    default:
        break;
    }
}

static void p4_verify_step(void)
{
    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    if (Vision_IsTimedOut()) {
        LowerCam_Light_Off();
        PrintDebug("[HOST] P4 verify timeout\r\n");
        Vision_ForceIdle();
        g_state = HOST_ERROR;
        return;
    }

    switch (vs) {
    case VISION_GOT_POS: {
        int32_t dx_s = (int32_t)(r->dy);
        int32_t dy_s = -(int32_t)(r->dx);
        if (dx_s != 0 || dy_s != 0) {
            safe_move_to(Coord_Get().x + dx_s, Coord_Get().y + dy_s, PNP_SPEED, PNP_ACC);
        }
        Vision_Go();
        break;
    }

    case VISION_DONE: {
        MachineCoord_t c = Coord_Get();
        int32_t e_x = c.x - g_p4_base_x;
        int32_t e_y = c.y - g_p4_base_y;
        LowerCam_Light_Off();
        Vision_SendEnd();
        PrintDebug("[HOST] P4 verify done: coord=(%ld,%ld) drift=(%ld,%ld)\r\n",
                   (long)c.x, (long)c.y, (long)e_x, (long)e_y);

        if (e_x != 0 || e_y != 0) {
            Coord_UpdateXY(c.x - e_x, c.y - e_y);
            g_pcb_frame.origin_x_steps -= e_x;
            g_pcb_frame.origin_y_steps -= e_y;
            g_mark_avg_dy -= e_x;
            g_mark_avg_dx -= e_y;
            PrintDebug("[HOST] Applied frame shift: (-%ld,-%ld)\r\n", (long)e_x, (long)e_y);
        }

        start_p1_find_first();
        break;
    }

    case VISION_ERROR:
        LowerCam_Light_Off();
        PrintDebug("[HOST] P4 verify ERROR: %s\r\n", Vision_GetError());
        Vision_ForceIdle();
        g_state = HOST_ERROR;
        break;

    default:
        break;
    }
}

/* 检查 P1 当前子位置是否超时 */
static bool p1_subpos_timed_out(void)
{
    return (osKernelGetTickCount() - g_p1_subpos_start_tick) >= pdMS_TO_TICKS(P1_SUBPOS_TIMEOUT_MS);
}

/* 找元件一步 (HOST_FIND_COMP 时调用) */
static void find_comp_step(void) {
    /* 首次进入贴装阶段时发送日志 */
    if (!s_pnp_phase_logged) {
        s_pnp_phase_logged = true;
        Bridge_NotifyLog(6, (uint8_t)g_comp_count);  /* code=6: PnP placing started */
    }

    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    if (Vision_IsTimedOut() || p1_subpos_timed_out()) {
        Vision_ForceIdle();
        Component_t *c = &g_components[g_comp_index];
        PrintDebug("[HOST] Find comp %u TIMEOUT, scan pos %d/%d\r\n",
                   c->id, g_p1_scan_pos, SCATTER_SUBPOS - 1);
        if (g_p1_scan_pos < SCATTER_SUBPOS - 1) {
            g_p1_scan_pos++;
            g_p1_found_pos  = -1;
            g_p1_subpos_start_tick = osKernelGetTickCount();
            int cl = component_cell(c);
            safe_move_to(g_scatter_subpos[cl][g_p1_scan_pos][0] + g_calib.cam_to_nozzle_dx_steps,
                         g_scatter_subpos[cl][g_p1_scan_pos][1] + g_calib.cam_to_nozzle_dy_steps,
                         PNP_SPEED, PNP_ACC);
            Vision_Start(VCMD_P1, footprint_to_class_id(c->footprint));
            return;
        }
        /* 所有子位耗尽 → 跳过该元件 */
        g_p1_scan_pos = 0;
        g_p1_subpos_start_tick = osKernelGetTickCount();
        g_consecutive_failures++;
        if (g_consecutive_failures >= 3) {
            host_send("REFILL_NEEDED");
            g_state = HOST_WAIT_REFILL;
        } else {
            g_comp_index++;
            if (g_comp_index >= g_comp_count) { g_state = HOST_DONE; }
            else {
                int cl = component_cell(&g_components[g_comp_index]);
                safe_move_to(g_scatter_subpos[cl][0][0] + g_calib.cam_to_nozzle_dx_steps, g_scatter_subpos[cl][0][1] + g_calib.cam_to_nozzle_dy_steps,
                             PNP_SPEED, PNP_ACC);
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
        g_p1_found_pos = g_p1_scan_pos;  /* 记住实际子位置 */
        Vision_ResetTimeout();            /* 全局超时重置，Phase1 独立计时 */
        g_p1_subpos_start_tick = osKernelGetTickCount();  /* 子位置超时重置 */
        g_p1_scan_pos = 0;   /* 复位扫描完成标记 */
        Vision_Go();
        break;

    case VISION_DONE: {
        /* P1 single-shot: apply vision offset then cam-to-nozzle compensation */
        Component_t *c = &g_components[g_comp_index];
        int32_t dx_s = -(int32_t)(r->dy); // cam Y → X1+X2, value already in motor steps
        int32_t dy_s = -(int32_t)(r->dx); // cam X → Y 电机, value already in motor steps

        PrintDebug("[HOST] Comp %u: cls=%s ang=%ld.%02ddeg offset(%ld,%ld)steps\r\n",
                   c->id, r->class_name,
                   (long)(r->angle_x100 / 100), (int)(r->angle_x100 % 100),
                   (long)r->dx, (long)r->dy);

        /* 应用视觉偏移 */
        if (dx_s != 0 || dy_s != 0) {
            safe_move_to(Coord_Get().x + dx_s, Coord_Get().y + dy_s, PNP_SPEED_FINE, PNP_ACC_FINE);
        }

        g_p1_retry_count = 0;
        g_p1_scan_pos = 0;
        g_consecutive_failures = 0;
        /* 摄像头→吸嘴偏置补偿 */
        if (g_calib.cam_to_nozzle_dx_steps != 0 || g_calib.cam_to_nozzle_dy_steps != 0) {
            safe_move_to(Coord_Get().x - g_calib.cam_to_nozzle_dx_steps,
                         Coord_Get().y - g_calib.cam_to_nozzle_dy_steps,
                         PNP_SPEED_FINE, PNP_ACC_FINE);
            PrintDebug("[HOST] Cam->Nozzle offset: (%ld,%ld) steps\r\n",
                       (long)g_calib.cam_to_nozzle_dx_steps, (long)g_calib.cam_to_nozzle_dy_steps);
        }
        PrintDebug("[HOST] Comp %u aligned. Picking...\r\n",
                   g_components[g_comp_index].id);
        g_state = HOST_PICK;
        break;
    }

    case VISION_ERROR: {
        const char *err = Vision_GetError();
        Component_t *c = &g_components[g_comp_index];
        bool is_not_found = (strcmp(err, "err1_5") == 0);
        bool recoverable  = is_not_found ||
                            (strcmp(err, "err1_8") == 0) ||
                            (strcmp(err, "err1_9") == 0);
        bool cam_fault    = (strcmp(err, "err1_1") == 0 ||
														 strcmp(err, "err1_3") == 0 ||
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
            g_p1_found_pos  = -1;
            g_p1_subpos_start_tick = osKernelGetTickCount();
            g_p1_retry_count = 0;
            int cl = component_cell(c);
            safe_move_to(g_scatter_subpos[cl][g_p1_scan_pos][0]  + g_calib.cam_to_nozzle_dx_steps,
                         g_scatter_subpos[cl][g_p1_scan_pos][1]  + g_calib.cam_to_nozzle_dy_steps,
                         PNP_SPEED_FINE, PNP_ACC_FINE);
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
            g_p1_found_pos  = -1;
            g_p1_subpos_start_tick = osKernelGetTickCount();
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
                    safe_move_to(g_scatter_subpos[cl][0][0] + g_calib.cam_to_nozzle_dx_steps, g_scatter_subpos[cl][0][1] + g_calib.cam_to_nozzle_dy_steps,
                                 PNP_SPEED, PNP_ACC);
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
    static int phase = 0;

    /* ---- Phase 1: 等待 R 轴旋转完成 ---- */
    if (phase == 1) {
        R_State_t rs = r_axis_state();
        if (rs == R_DONE) {
            osDelay(10);
            r_axis_set_zero();
            Vision_SendEnd();
            Bridge_NotifySMTProgress(g_comp_index + 1, g_comp_count);
            PrintDebug("[HOST] Offset check done, moving to PCB...\r\n");
            phase = 0;
            g_state = HOST_MOVE_TO_PCB;
            return;
        } else if (rs == R_STALL || rs == R_STUCK || rs == R_TIMEOUT) {
            PrintDebug("[HOST] P3: R correction FAILED (state=%d)\r\n", (int)rs);
            Vision_SendEnd();
            phase = 0;
            g_state = HOST_ERROR;
            return;
        }
        /* R_BUSY: 继续等待 */
        return;
    }

    /* ---- Phase 0: 正常视觉轮询 ---- */
    VisionState_t vs = Vision_GetState();
    const VisionResult_t *r = Vision_GetResult();

    if (Vision_IsTimedOut()) {
        LowerCam_Light_Off();
        PrintDebug("[HOST] Offset check timeout, skipping to place\r\n");
        g_p3_nozzle_retry = 0;  /* 超时降级，清零重试计数 */
        Coord_Invalidate();  /* R 轴位置未经 P3 验证 */
        Vision_ForceIdle();
        g_state = HOST_MOVE_TO_PCB;
        return;
    }

    switch (vs) {
    case VISION_DONE: {
        /* P3 single-shot: apply vision offset + accumulate */
        int32_t dx_s = (int32_t)(r->dy);  // cam Y → X1+X2，不取反, value already in motor steps
        int32_t dy_s = -(int32_t)(r->dx); // cam X → Y 电机，取反, value already in motor steps
        if (dx_s != 0 || dy_s != 0) {
            safe_move_to(Coord_Get().x + dx_s, Coord_Get().y + dy_s, PNP_SPEED, PNP_ACC);
            g_p3_offset_x += dx_s;
            g_p3_offset_y += dy_s;
        }

        /* 角度矫正: 启动非阻塞旋转，完成后过渡到 PCB */
        g_p3_nozzle_retry = 0;
        LowerCam_Light_Off();
        if (host_start_r_correction(r, "P3")) {
            phase = 1;
            return;   /* 等待 r_axis_poll 完成旋转 */
        }
        /* 无需矫正，直接过渡 */
        osDelay(10);
        r_axis_set_zero();
        Vision_SendEnd();
        Bridge_NotifySMTProgress(g_comp_index + 1, g_comp_count);
        PrintDebug("[HOST] Offset check done, moving to PCB...\r\n");
        g_state = HOST_MOVE_TO_PCB;
        break;
    }

    case VISION_ERROR: {
        const char *err = Vision_GetError();
        Component_t *c = &g_components[g_comp_index];
        int cl;
        /* err3_8: 吸嘴空取 — 回退重新吸取，不降级贴装 */
        if (err[0] == 'e' && err[1] == 'r' && err[2] == 'r' &&
            err[3] == '3' && err[4] == '_' && err[5] == '8' && err[6] == '\0') {
            g_p3_nozzle_retry++;
            if (g_p3_nozzle_retry >= 3) {
                LowerCam_Light_Off();
                PrintDebug("[HOST] P3 nozzle empty x3, check feeder!\r\n");
                g_state = HOST_ERROR;
            } else {
                LowerCam_Light_Off();
                PrintDebug("[HOST] P3 nozzle empty, retry pickup (%d/3)\r\n", g_p3_nozzle_retry);
                /* 回退散料区重新 P1 找取同一元件 */
                cl = component_cell(c);
                int pos = g_p1_found_pos >= 0 ? g_p1_found_pos : 0;
                g_p1_found_pos = -1;  /* 清空，重试 VISION_GOT_STOP 会重新设置 */
                g_p1_scan_pos = pos;
                g_p1_subpos_start_tick = osKernelGetTickCount();
                safe_move_to(g_scatter_subpos[cl][pos][0] + g_calib.cam_to_nozzle_dx_steps, g_scatter_subpos[cl][pos][1] + g_calib.cam_to_nozzle_dy_steps,
                             PNP_SPEED, PNP_ACC);
                Vision_Start(VCMD_P1, footprint_to_class_id(c->footprint));
                g_state = HOST_FIND_COMP;
            }
        } else {
            LowerCam_Light_Off();
            PrintDebug("[HOST] Offset check ERROR: %s\r\n", err);
            Coord_Invalidate();  /* P3 失败，R 轴位置不可信 */
            g_state = HOST_MOVE_TO_PCB;  /* 容错：仍然贴装 */
        }
        break;
    }

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
/* ================================================================
 *  PnP step 函数
 * ================================================================ */

static void pick_step(void) {
    static int phase = 0;

    Component_t *c = &g_components[g_comp_index];

    /* ---- Phase 1: 等待 R 轴旋转完成 ---- */
    if (phase == 1) {
        R_State_t rs = r_axis_state();
        if (rs == R_DONE) {
            osDelay(10);  /* 机械停稳 */
            r_axis_set_zero();
            phase = 0;
            g_state = HOST_MOVE_TO_BOTTOM_CAM;
            return;
        } else if (rs == R_STALL || rs == R_STUCK || rs == R_TIMEOUT) {
            PrintDebug("[HOST] PICK: R correction FAILED (state=%d)\r\n", (int)rs);
            phase = 0;
            g_state = HOST_ERROR;
            return;
        }
        /* R_BUSY: 继续等待 r_axis_poll 推进 */
        return;
    }

    /* ---- Phase 0: 吸取元件 ---- */
    PrintDebug("[HOST] PICK comp %u\r\n", c->id);
    if (!pick_component()) {
        PrintDebug("[HOST] Pick FAILED, retrying at found pos\r\n");
        int cl = component_cell(c);
        int pos = g_p1_found_pos >= 0 ? g_p1_found_pos : 0;
        g_p1_found_pos = -1;  /* 清空，重试 VISION_GOT_STOP 会重新设置 */
        g_p1_scan_pos = pos;
        g_p1_subpos_start_tick = osKernelGetTickCount();
        safe_move_to(g_scatter_subpos[cl][pos][0] + g_calib.cam_to_nozzle_dx_steps, g_scatter_subpos[cl][pos][1] + g_calib.cam_to_nozzle_dy_steps, PNP_SPEED, PNP_ACC);
        Vision_Start(VCMD_P1, footprint_to_class_id(c->footprint));
        g_state = HOST_FIND_COMP;
        return;
    }

    /* R轴矫正: P1识别完成，吸取后旋转。无矫正则直接过渡 */
    if (host_start_r_correction(Vision_GetResult(), "P1")) {
        phase = 1;
        return;   /* 等待 r_axis_poll 完成旋转 */
    }
    /* 无需矫正，直接过渡 */
    r_axis_set_zero();
    g_state = HOST_MOVE_TO_BOTTOM_CAM;
}

static void move_to_bottom_step(void) {
    g_p3_offset_x = 0; g_p3_offset_y = 0;
    safe_move_to(g_calib.bottom_cam_x_steps, g_calib.bottom_cam_y_steps, PNP_SPEED, PNP_ACC);
    LowerCam_Light_On();
    Vision_Start(VCMD_P3, 0);
    g_state = HOST_OFFSET_CHECK;
}

static void move_to_pcb_step(void) {
    static int     phase = 0;
    static int32_t saved_mx, saved_my;

    /* pick 确认成功，子位置信息生命周期结束 */
    g_p1_found_pos = -1;

    /* ---- Phase 1: 等待 R 轴旋转完成，然后 XY 移动 ---- */
    if (phase == 1) {
        R_State_t rs = r_axis_state();
        if (rs == R_DONE) {
            PrintDebug("[HOST] MOVE_TO_PCB: current=(%ld,%ld) target=(%ld,%ld)\r\n",
                       (long)Coord_Get().x, (long)Coord_Get().y,
                       (long)saved_mx, (long)saved_my);
            safe_move_to(saved_mx, saved_my, PNP_SPEED_FAST, PNP_ACC);
            phase = 0;
            g_state = HOST_PLACE;
            return;
        } else if (rs == R_STALL || rs == R_STUCK || rs == R_TIMEOUT) {
            PrintDebug("[HOST] MOVE_TO_PCB: R rotation FAILED (state=%d)\r\n", (int)rs);
            phase = 0;
            g_state = HOST_ERROR;
            return;
        }
        /* R_BUSY: 继续等待 */
        return;
    }

    /* ---- Phase 0: 计算坐标 + 启动 R 旋转 ---- */
    Component_t *c = &g_components[g_comp_index];
    int32_t cx = (int32_t)(c->target_x * STEPS_PER_MM);
    int32_t cy = (int32_t)(c->target_y * STEPS_PER_MM);
    int32_t machine_x, machine_y;
    if (g_pcb_frame.valid) {
        float cos_t = cosf(g_pcb_frame.rotation_rad), sin_t = sinf(g_pcb_frame.rotation_rad);
        /* 标准旋转 (相机坐标系) → 电机坐标: cam_X→Y电机(取反), cam_Y→X1+X2 */
        float rcx = cx * cos_t - cy * sin_t;
        float rcy = cx * sin_t + cy * cos_t;
        machine_x = (int32_t)rcy + g_pcb_frame.origin_x_steps + g_p3_offset_x;
        machine_y = (int32_t)(-rcx) + g_pcb_frame.origin_y_steps + g_p3_offset_y;
    } else {
        /* g_mark_avg_dx/dy 已是电机步数, 直接加 */
        machine_x = cy + g_mark_avg_dy;
        machine_y = cx + g_mark_avg_dx;
    }
    PrintDebug("[HOST] MOVE_TO_PCB comp %u -> machine(%ld,%ld)\r\n", c->id, (long)machine_x, (long)machine_y);

    /* 保存目标坐标，Phase 1 使用 */
    saved_mx = machine_x;
    saved_my = machine_y;

    /* 启动 R 轴旋转到贴装角度 */
    r_axis_start(-c->target_angle, R_SPEED_RPM);
    phase = 1;
}

static void place_step(void) {
    Component_t *c = &g_components[g_comp_index];
    place_component(); c->placed = true; g_comp_index++;
    if (g_comp_index >= g_comp_count) { g_state = HOST_DONE; }
    else {
        int cl = component_cell(&g_components[g_comp_index]); g_p1_scan_pos = 0;
        g_p1_found_pos  = -1;
        g_p1_subpos_start_tick = osKernelGetTickCount();
        PrintDebug("[HOST] PLACE: moving to scatter cell=%d subpos=0 target=(%ld,%ld)\r\n",
                   cl, (long)(g_scatter_subpos[cl][0][0] + g_calib.cam_to_nozzle_dx_steps),
                   (long)(g_scatter_subpos[cl][0][1] + g_calib.cam_to_nozzle_dy_steps));
        if (safe_move_to(g_scatter_subpos[cl][0][0] + g_calib.cam_to_nozzle_dx_steps, g_scatter_subpos[cl][0][1] + g_calib.cam_to_nozzle_dy_steps, PNP_SPEED_FAST, PNP_ACC) != 0) {
            PrintDebug("[HOST] PLACE: scatter move FAILED, entering ERROR\r\n");
            g_state = HOST_ERROR;
        } else {
            PrintDebug("[HOST] PLACE: scatter move OK, starting P1 for comp %u\r\n", g_components[g_comp_index].id);
            Vision_Start(VCMD_P1, footprint_to_class_id(g_components[g_comp_index].footprint));
            g_state = HOST_FIND_COMP;
        }
    }
}

static void done_step(void) {
        Bridge_NotifySMTProgress(g_comp_count, g_comp_count);
    PrintDebug("[HOST] All %u components placed!\r\n", g_comp_count);
    { uint8_t d[8]={0}; d[0]=(uint8_t)(g_comp_count>>8); d[1]=(uint8_t)g_comp_count; Log_Write(LOG_PNP_DONE,d); }
    g_comp_count = 0; g_mark_count = 0;
    safe_move_to(0, 0, PNP_SPEED_FAST, PNP_ACC);
    osDelay(200);
    if (g_auto_heat) {
        Heater_SendStart(); { uint8_t d[8]={0}; Log_Write(LOG_HEATER_START,d); }
        Bridge_NotifyLog(7, 0);  /* code=7: Reflow started */
        PrintDebug("[HOST] Auto reflow started\r\n"); host_send("REFLOW_STARTED");
        g_state = HOST_REFLOW;
    } else { g_state = HOST_DEBUG; }
}

static void reflow_step(void) {
    HeaterStatus_t hs = Heater_GetCurrentStatus();
    /* timestamp=0 表示尚未收到任何状态帧，不能做判断 */
    if (hs.timestamp == 0) { osDelay(200); return; }
    /* 从机实际只发送 IDLE/HEATING/COOLING/ERROR，HOLDING/COMPLETE 为遗留字段 */
    if (hs.state == HEATER_STATE_IDLE || hs.state == HEATER_STATE_COMPLETE) {
        { uint8_t d[8]={0}; d[0]=hs.state; Log_Write(LOG_HEATER_DONE,d); }
        PrintDebug("[HOST] Reflow complete (state=%d)\r\n", hs.state); host_send("REFLOW_DONE");
        g_state = HOST_DEBUG;
    } else if (hs.state == HEATER_STATE_ERROR) {
        { uint8_t d[8]={0}; d[0]=HEATER_STATE_ERROR; Log_Write(LOG_HEATER_DONE,d); }
        PrintDebug("[HOST] Reflow ERROR!\r\n"); host_send("REFLOW_ERROR");
        g_state = HOST_ERROR;
    } else {
        osDelay(200);
    }
}

static void error_step(void) {
    if (!g_error_entered) {
        Heater_SendStop(); z_safe();
        { uint8_t d[8]={0}; d[0]=1; d[1]=(uint8_t)(g_comp_index>>8); d[2]=(uint8_t)g_comp_index; Log_Write(LOG_PNP_ERROR,d); }
        PrintDebug("[HOST] ERROR state. Waiting RESUME or ABORT (30s timeout)...\r\n");
        host_send("ERROR");
        g_error_entered = true;
        g_error_start_tick = osKernelGetTickCount();
    }
    if ((osKernelGetTickCount() - g_error_start_tick) >= pdMS_TO_TICKS(30000)) {
        PrintDebug("[HOST] ERROR timeout, auto-returning to DEBUG.\r\n");
        g_comp_count = 0; g_mark_count = 0; g_comp_index = 0;
        g_p3_nozzle_retry = 0;
        g_error_entered = false;
        g_state = HOST_DEBUG;
    }
}

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

#ifndef SKIP_CAM_HANDSHAKE
    /* P0 握手：与 MaixCAM 建立连接 */
    if (!Vision_Handshake(120000)) {
        PrintDebug("[HOST] P0 handshake FAILED! Camera not responding.\r\n");
        host_send("CAM_ERROR");
    } else {
        PrintDebug("[HOST] P0 handshake OK.\r\n");
    }
#else
    PrintDebug("[HOST] P0 handshake SKIPPED.\r\n");
#endif

    g_state = HOST_HOME;
    g_comp_count = 0;
    g_comp_index = 0;
    g_mark_count = 0;
    g_header_parsed = false;
    Coord_Init();
    g_jog_active = false;
    memset(g_components, 0, sizeof(g_components));
    memset(g_marks, 0, sizeof(g_marks));
    memset(g_mark_offsets, 0, sizeof(g_mark_offsets));

    /* 加热台队列初始化（须在 CAN_Init 前，确保 ISR 触发时队列已存在） */
    Heater_Init();

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

    Log_Init();

    /* 从 Flash 加载标定值 */
    if (Calib_Load(&g_calib) != 0) {
        PrintDebug("[HOST] Flash read failed, using defaults.\r\n");
    }
    scatter_init_cells();
    PrintDebug("[HOST] Calib: z_safe=%.1f z_pick=%.1f z_place=%.1f\r\n",
               (double)g_calib.z_safe_angle, (double)g_calib.z_pick_angle, (double)g_calib.z_place_angle);
    PrintDebug("[HOST] Calib: scatter=(%ld,%ld) size=%ld\r\n",
               (long)g_calib.scatter_x_steps, (long)g_calib.scatter_y_steps, (long)g_calib.scatter_size_steps);
    PrintDebug("[HOST] Calib: heat=(%ld,%ld)-(%ld,%ld) botcam=(%ld,%ld)\r\n",
               (long)g_calib.heat_platform_x_min, (long)g_calib.heat_platform_y_min,
               (long)g_calib.heat_platform_x_max, (long)g_calib.heat_platform_y_max,
               (long)g_calib.bottom_cam_x_steps, (long)g_calib.bottom_cam_y_steps);
    PrintDebug("[HOST] Calib: nozzle_off=(%ld,%ld) cam_p1=%.3f cam_p3=%.3f\r\n",
               (long)g_calib.cam_to_nozzle_dx_steps, (long)g_calib.cam_to_nozzle_dy_steps,
               (double)g_calib.cam_p1_val_to_steps, (double)g_calib.cam_p3_val_to_steps);
    PrintDebug("[HOST] Task started. Waiting for SET_ORIGIN...\r\n");

    /* 主动通知上位机进入调试模式，解除按钮死锁 */
    UART_SendString(UART_CH1, "DEBUG_MODE\n");
    g_state = HOST_DEBUG;

    /* ---- TouchGFX 屏幕对接初始化 ---- */
    Bridge_Init();
    Bridge_NotifyMotorSpeed(PNP_SPEED_FAST);

    /* ===== 主循环 ===== */
    for (;;) {
        /* ---- 0. R 轴状态机推进（非阻塞） ---- */
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
                        if (g_state == HOST_DEBUG
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
                            Bridge_NotifyDownloadStatus(1);
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
        /* 屏幕温度更新 */
        Bridge_ProcessHeaterStatus();

        /* ---- 4. 下载超时检测 ---- */
        if (g_state == HOST_DOWNLOADING) {
            uint32_t elapsed = osKernelGetTickCount() - g_last_line_tick;
            if (elapsed >= DOWNLOAD_TIMEOUT_MS) {
                download_done();
                continue;
            }
        }

        /* ---- 4.5 GUI 暂停检查：任意贴片状态可响应 ---- */
        if (g_gui_smt_pause_req &&
            g_state >= HOST_FIND_COMP && g_state <= HOST_PLACE) {
            g_gui_smt_pause_req = 0;
            Bridge_NotifySMTStatus(0);
            Bridge_NotifyLog(4, 0);  /* code=4: SMT paused */
            PrintDebug("[HOST] GUI: SMT paused.\r\n");
            Vision_SendEnd();
            Vision_ForceIdle();
            z_safe();
            g_state = HOST_DEBUG;
            continue;
        }

        /* ---- 5. 状态机 ---- */
        switch (g_state) {

        case HOST_HOME:
            /* 等待 SET_ORIGIN 归零 */
            osDelay(100);
            break;

        case HOST_DEBUG: {
            /* 检查 GUI 触摸屏命令 */
            if (g_gui_smt_start_req) {
                g_gui_smt_start_req = 0;
                /* GUI 请求开始贴片：发送下载就绪，进入下载模式 */
                UART_SendString(UART_CH1, "DOWNLOAD_READY\n");
                g_comp_count = 0;
                g_mark_count = 0;
                g_header_parsed = false;
                g_state = HOST_DOWNLOADING;
                Bridge_NotifyDownloadStatus(1);
                Bridge_NotifyLog(1, 0);  /* code=1: GUI triggered download */
                PrintDebug("[HOST] GUI: download started.\r\n");
            }
            /* 处理 GUI 电机运动命令（从 motion_cmd_queue） */
            {
                MotionCmd_t mcmd;
                while (motion_cmd_queue != NULL &&
                       osMessageQueueGet(motion_cmd_queue, &mcmd, NULL, 0) == osOK) {
                    switch (mcmd.cmd_type) {
                        case MOTION_CMD_MOVE_TO:
                            safe_move_to(mcmd.target_x, mcmd.target_y, mcmd.speed, mcmd.acc);
                            break;
                        case MOTION_CMD_STOP: {
                            axis_stop(X1_ADDR);
                            axis_stop(X2_ADDR);
                            axis_stop(Y_ADDR);
                            disable_sync_stop();
                            motorSyncTrigger(0x00);
                            break;
                        }
                        case MOTION_CMD_HOME: {
                            /* 归零：移动到原点 */
                            safe_move_to(0, 0, PNP_SPEED_FAST, PNP_ACC);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
            osDelay(10);
            break;
        }

        case HOST_DOWNLOADING:
            osDelay(10);
            break;

        case HOST_MARK_ALIGN:
            mark_align_step();
            break;

        case HOST_P4_BASELINE:
            p4_baseline_step();
            break;

        case HOST_P4_VERIFY:
            p4_verify_step();
            break;

        case HOST_FIND_COMP:
            find_comp_step();
            break;

        case HOST_PICK:              pick_step();            break;
        case HOST_MOVE_TO_BOTTOM_CAM: move_to_bottom_step(); break;
        case HOST_OFFSET_CHECK:      offset_check_step();    break;
        case HOST_MOVE_TO_PCB:       move_to_pcb_step();     break;
        case HOST_PLACE:             place_step();           break;
        case HOST_DONE: {
            if (!s_bridge_done_notified) {
                Bridge_NotifySMTStatus(0);
                Bridge_NotifyLog(2, 0);  /* code=2: PnP complete */
                s_bridge_done_notified = 1;
            }
            done_step();
            break;
        }
        case HOST_REFLOW:            reflow_step();          break;
        case HOST_WAIT_REFILL:       osDelay(200);           break;
        case HOST_ERROR:             error_step();           break;

        default:
            break;
        }

        /* ---- 电机错误检测 ---- */
        if (g_motor_error) {
            g_motor_error = false;
            if (g_state != HOST_DEBUG && g_state != HOST_HOME) {
                { uint8_t d[8] = {0}; d[0] = (uint8_t)g_motor_error_detail; Log_Write(LOG_MOTOR_ERROR, d); }
                Bridge_NotifyLog(3, (uint8_t)g_motor_error_detail);
                if (g_motor_error_detail == MOTOR_ERR_TIMEOUT) {
                    PrintDebug("[HOST] Motor TIMEOUT, coordinates invalidated.\r\n");
                } else {
                    PrintDebug("[HOST] Motor LIMIT/BLOCK!\r\n");
                }
                /* 坐标已不可信，进入错误态等待 ABORT + SET_ORIGIN 恢复 */
                Vision_SendEnd();
                Vision_ForceIdle();
                z_safe();
                g_state = HOST_ERROR;
            }
        }
    }
}