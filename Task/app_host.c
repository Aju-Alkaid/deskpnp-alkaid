#include "app_host.h"
#include "app_test.h"
#include "driver_uart.h"
#include "driver_can.h"
#include "driver_motor.h"
#include "driver_servo.h"
#include "driver_drv8803.h"
#include "driver_tmc2209.h"
#include "driver_heater.h"
#include "driver_spiflash_w25q64.h"

extern TIM_HandleTypeDef htim2;  /* Z轴舵机 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ================================================================
 *  常量
 * ================================================================ */
#define DEBUG_SPEED     300            /* 调试模式离散移动速度 */
#define DEBUG_ACC       25             /* 调试模式加速度 */
#define JOG_SPEED        300            /* 连续移动速度 (RPM) */
#define JOG_ACC          25             /* 连续移动加速度 */
#define JOG_MMS_TO_RPM  12.0f   /* mm/s → RPM: STEPS_PER_MM/16384*60 */
#define PNP_SPEED        300
#define PNP_ACC          25
#define PICK_DELAY_MS    300
#define PLACE_DELAY_MS   300
#define PUMP_BLOW_MS    1000          /* 关气泵后电磁阀吹气时长(ms) */
#define Z_SERVO_CH       2            /* 舵机通道号 */

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


/* 封装名->P1类别ID映射 (0=ccapt 1=cledy 2=cledo 3=crest) */
static int footprint_to_class_id(const char *fp) {
    if (!fp) return 0;
    if (strncmp(fp, "LED", 3) == 0) return 2;   /* LED-SMD -> cledo */
    if (strncmp(fp, "C0", 2) == 0 || strncmp(fp, "R0", 2) == 0) return 0;
    return 0;
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
    g_header_parsed = false;

    if (g_mark_count > 0) {
        g_comp_index = 0;
        g_mark_count_done = 0;
        memset(g_mark_offsets, 0, sizeof(g_mark_offsets));
        g_state = HOST_MARK_ALIGN;
        Vision_Start(VCMD_P2, 0);
        Vision_Go();
        PrintDebug("[HOST] Starting Mark alignment (P2, %u marks)...\r\n", g_mark_count);
    } else if (g_comp_count > 0) {
        g_comp_index = 0;
        move_xy_relative(g_calib.scatter_x_steps - g_cur_x,
                         g_calib.scatter_y_steps - g_cur_y,
                         PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
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
    /* 状态去重：连续两次完全相同的命令直接丢弃
     * 相比时间窗口防抖，此方案不依赖 tick 精度，中间有别的命令自动复位 */
    if (cmd->cmd == g_last_cmd && cmd->param == g_last_param) {
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
        int ret = move_xy_relative(dx, dy, DEBUG_SPEED, DEBUG_ACC, &g_cur_x, &g_cur_y);
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
        int32_t dx = tx - g_cur_x;
        int32_t dy = ty - g_cur_y;
        move_xy_relative(dx, dy, DEBUG_SPEED, DEBUG_ACC, &g_cur_x, &g_cur_y);
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
        g_state = HOST_INIT;
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
        PrintDebug("[HOST] Mark alignment complete. %d marks. Avg offset: (%ld,%ld)mm10000\r\n",
                   g_mark_count_done, (long)g_mark_avg_dx, (long)g_mark_avg_dy);

        /* Mark 完成后：如果有贴装元件，开始 P1 找元件流程 */
        if (g_comp_count > 0) {
            move_xy_relative(g_calib.scatter_x_steps - g_cur_x, g_calib.scatter_y_steps - g_cur_y,
                             PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            Vision_Start(VCMD_P1, footprint_to_class_id(g_components[0].footprint));
            g_state = HOST_FIND_COMP;
        } else {
            /* 仅有 Mark 点、无贴装元件 */
            host_send("DOWNLOAD_READY");
            g_state = HOST_DEBUG;
        }
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
    case VISION_GOT_CATEGORY_QUERY:
        /* P1 类别询问：Cam 需要知道元件类别，回复 cls */
        Vision_ClsReply();
        break;
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
        g_p1_retry_count = 0;
        PrintDebug("[HOST] Comp %u aligned. Picking...\r\n",
                   g_components[g_comp_index].id);
        g_state = HOST_PICK;
        break;

    case VISION_ERROR: {
        const char *err = Vision_GetError();
        Component_t *c = &g_components[g_comp_index];
        bool recoverable = (strcmp(err, "err1_5") == 0 ||
                            strcmp(err, "err1_8") == 0 ||
                            strcmp(err, "err1_9") == 0);
        bool cam_fault   = (strcmp(err, "err1_1") == 0 ||
                            strcmp(err, "err1_4") == 0);
        if ((recoverable || cam_fault) && g_p1_retry_count < 3) {
            g_p1_retry_count++;
            PrintDebug("[HOST] Comp %u %s, retry P1 (%d/3)...\r\n",
                       c->id, err, g_p1_retry_count);
            Vision_Start(VCMD_P1, footprint_to_class_id(c->footprint));
        } else {
            PrintDebug("[HOST] Find comp %u ERROR: %s (retries=%d), skipping\r\n",
                       c->id, err, g_p1_retry_count);
            g_p1_retry_count = 0;
            g_comp_index++;
            if (g_comp_index >= g_comp_count) {
                g_state = HOST_DONE;
            } else {
                Vision_Start(VCMD_P1, footprint_to_class_id(g_components[g_comp_index].footprint));
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
    g_state = HOST_INIT;
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

    /* 从 Flash 加载标定值 */
    if (Calib_Load(&g_calib) != 0) {
        PrintDebug("[HOST] Flash read failed, using defaults.\r\n");
    }
    PrintDebug("[HOST] Calib: scatter=(%ld,%ld) botcam=(%ld,%ld)\r\n",
               (long)g_calib.scatter_x_steps, (long)g_calib.scatter_y_steps,
               (long)g_calib.bottom_cam_x_steps, (long)g_calib.bottom_cam_y_steps);

    PrintDebug("[HOST] Task started.\r\n");

    /* 启动握手：发送 DEBUG_MODE，进入调试模式 */
    UART_SendString(UART_CH1, "DEBUG_MODE\n");
    osDelay(50);
    g_state = HOST_DEBUG;



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
                        if ((g_state == HOST_DEBUG || g_state == HOST_INIT)
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
            move_xy_relative(g_calib.bottom_cam_x_steps - g_cur_x, g_calib.bottom_cam_y_steps - g_cur_y,
                             PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            Vision_Start(VCMD_P3, 0);
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
            move_xy_relative(g_calib.scatter_x_steps - g_cur_x, g_calib.scatter_y_steps - g_cur_y,
                             PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
            Vision_Start(VCMD_P1, footprint_to_class_id(g_components[g_comp_index].footprint));
            g_state = HOST_FIND_COMP;
        }
        break;
        }
        case HOST_DONE:
            PrintDebug("[HOST] All %u components placed!\r\n", g_comp_count);
            g_comp_count = 0;
            g_mark_count = 0;
            osDelay(200);
            /* 不再自动发 DOWNLOAD_READY，避免锁调试按钮 */
            g_state = HOST_DEBUG;
            break;

        case HOST_ERROR:
            PrintDebug("[HOST] ERROR state. Resetting to DEBUG.\r\n");
            g_comp_count = 0;
            g_mark_count = 0;
            g_comp_index = 0;
            osDelay(500);
            /* 不再自动发 DOWNLOAD_READY，避免锁调试按钮 */
            g_state = HOST_DEBUG;
            break;

        default:
            break;
        }
    }
}