#include "app_vision.h"
#include "app_host.h"
#include "app_test.h"
#include "driver_uart.h"
#include <string.h>

/* ================================================================
 *  帧协议常量 (MaixCAM 2026)
 *  格式: 0x7E | LEN(1B) | PAYLOAD(LEN 字节, UTF-8) | 0x7F
 * ================================================================ */
#define FRAME_HEAD         0x7E
#define FRAME_TAIL         0x7F
#define FRAME_PAYLOAD_MAX  255
#define P1_MAX_ITER        5
#define P3_MAX_ITER        5

/* ================================================================
 *  帧解析器状态
 * ================================================================ */
typedef enum {
    FS_WAIT_HEAD,        /* 等待 0x7E */
    FS_WAIT_LEN,         /* 等待 LEN 字节 */
    FS_WAIT_DATA,        /* 接收 PAYLOAD */
    FS_WAIT_TAIL,        /* 等待 0x7F */
} FrameState_t;

/* ================================================================
 *  Process1 内部子状态
 * ================================================================ */
typedef enum {
    P1_S0_WAIT_STOP,     /* Phase 0: 已发 "p1"，等待 "stp" */
    P1_S1_WAIT_POS,      /* Phase 1: 已发 "go"，等待初始 pos (含角度+类别) */
    P1_S2_ITERATING,     /* Phase 2: 迭代对齐，等待 iter pos 或 "ok" */
} P1_State_t;

/* ================================================================
 *  Process2 内部子状态
 *
 *  P2 的 "go" 是多态命令，语义取决于当前子状态:
 *   IDLE       → go → SEARCHING  (开始搜索第一个 Mark)
 *   WAIT_GO    → go → SEARCHING  (搜索下一个 Mark)
 *   SEARCHING  → got stp → 宿主发 go → POS_DETECT (精确测位)
 *   POS_DETECT → got pos → 宿主发 go → ALIGNING (迭代归零)
 *   ALIGNING   → got ok  → 当前 Mark 完成
 *   非末Mark ok → WAIT_GO + GOT_STOP (驱动下一轮)
 *   末Mark ok  → DONE
 * ================================================================ */
typedef enum {
    P2_IDLE,             /* 刚发 "p2"，等待首次 Vision_Go() */
    P2_WAIT_GO,          /* 非末 Mark "ok" 后，等待宿主发 "go" 搜索下一个 */
    P2_SEARCHING,        /* 已发 "go"，搜索 Mark，等待 "stp" */
    P2_POS_DETECT,       /* 已发 "go"，精确测位，等待 "pos" 数据 */
    P2_ALIGNING,         /* 已发 "go"，迭代归零，等待 "ok" */
} P2_State_t;

/* ================================================================
 *  Process3 内部子状态
 * ================================================================ */
typedef enum {
    P3_PHASE1,           /* 刚发 "p3"，等待初始 "pos" (自动检测) */
    P3_PHASE2,           /* 已发 "go"，迭代修正，等待 iter pos 或 "ok" */
} P3_State_t;

/* ================================================================
 *  全局状态
 * ================================================================ */

/* ---- 帧解析器 ---- */
static FrameState_t  g_fs           = FS_WAIT_HEAD;
static uint8_t       g_frame_len    = 0;
static uint8_t       g_frame_idx    = 0;
static uint8_t       g_frame_buf[FRAME_PAYLOAD_MAX + 1];  /* +1 for null term */

/* ---- 对外状态 ---- */
static VisionState_t g_state        = VISION_IDLE;
static VisionCmd_t   g_active_cmd   = VCMD_P1;

/* ---- Process1 子状态 ---- */
static P1_State_t    g_p1_sub       = P1_S0_WAIT_STOP;
static int           g_p1_iter      = 0;

/* ---- Process2 子状态 ---- */
static P2_State_t    g_p2_sub       = P2_IDLE;
static int           g_p2_mark_idx  = 0;    /* 当前 Mark 序号 (0-based) */

/* ---- Process3 子状态 ---- */
static P3_State_t    g_p3_sub       = P3_PHASE1;
static int           g_p3_iter      = 0;

/* ---- 字段收集状态 ---- */
static bool          g_collecting   = false;
static int           g_collect_cnt  = 0;
static int           g_collect_exp  = 0;

/* ---- 收集暂存 ---- */
static int32_t       g_tmp_dx       = 0;
static int32_t       g_tmp_dy       = 0;
static int32_t       g_tmp_ang      = 0;
static char          g_tmp_cls[8]   = {0};

/* ---- 结果与错误 ---- */
static VisionResult_t g_result;
static char           g_error_code[8];

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/* 解析 "N:xxx" 数字帧，返回 true 表示成功 */
static bool parse_number_frame(const char *str, int32_t *out) {
    if (str[0] != (char)0x4E || str[1] != (char)0x3A) return false;  /* 'N' ':' */
    const char *p = str + 2;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    int32_t val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (int32_t)(*p - '0');
        p++;
    }
    *out = val * sign;
    return true;
}

/* 组帧发送: 0x7E LEN payload 0x7F */
static void send_frame(const char *str) {
    uint8_t len = (uint8_t)strlen(str);
    if (len > FRAME_PAYLOAD_MAX) len = FRAME_PAYLOAD_MAX;
    uint8_t buf[FRAME_PAYLOAD_MAX + 3];
    buf[0] = FRAME_HEAD;
    buf[1] = len;
    if (len > 0) {
        memcpy(buf + 2, str, len);
    }
    buf[2 + len] = FRAME_TAIL;
    HAL_UART_Transmit(&huart2, buf, (uint16_t)(len + 3), 100);
}

/* 重置所有内部状态 */
static void reset_all(void) {
    g_fs = FS_WAIT_HEAD;       /* 重置帧解析器，防止跨 Process 残帧错位 */
    g_state       = VISION_IDLE;
    g_active_cmd  = VCMD_P1;
    g_p1_sub      = P1_S0_WAIT_STOP;
    g_p1_iter     = 0;
    g_p2_sub      = P2_IDLE;
    g_p2_mark_idx = 0;
    g_p3_sub      = P3_PHASE1;
    g_p3_iter     = 0;
    g_collecting  = false;
    g_collect_cnt = 0;
    memset(&g_result, 0, sizeof(g_result));
    memset(g_error_code, 0, sizeof(g_error_code));
}

/* 重置字段收集 */
static void collect_begin(int expected) {
    g_collecting  = true;
    g_collect_cnt = 0;
    g_collect_exp = expected;
    g_tmp_dx  = 0;
    g_tmp_dy  = 0;
    g_tmp_ang = 0;
    memset(g_tmp_cls, 0, sizeof(g_tmp_cls));
}

/* 安全拷贝字符串到固定大小 buffer */
static void safe_strcpy(char *dst, const char *src, int dst_size) {
    int i = 0;
    while (src[i] && i < dst_size - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* 安全拷贝错误码 */
static void save_error(const char *str) {
    safe_strcpy(g_error_code, str, sizeof(g_error_code));
}

/* ================================================================
 *  Process1 帧分发 (ISR 上下文，禁止 PrintDebug / 阻塞调用)
 * ================================================================ */
static void process_p1_frame(const char *str) {
    /* ---- 错误帧优先级最高 ---- */
    if (str[0] == 'e' && str[1] == 'r' && str[2] == 'r') {
        g_collecting = false;
        g_state = VISION_ERROR;
        save_error(str);
        return;
    }

    /* ---- "stp" ---- */
    if (str[0] == 's' && str[1] == 't' && str[2] == 'p' && str[3] == '\0') {
        g_collecting = false;
        if (g_p1_sub == P1_S0_WAIT_STOP) {
            g_state = VISION_GOT_STOP;
        }
        return;
    }

    /* ---- "ok" ---- */
    if (str[0] == 'o' && str[1] == 'k' && str[2] == '\0') {
        g_collecting = false;
        g_state = VISION_DONE;
        return;
    }

    /* ---- "pos" — 开始位置数据序列 ---- */
    if (str[0] == 'p' && str[1] == 'o' && str[2] == 's' && str[3] == '\0') {
        if (g_p1_sub == P1_S1_WAIT_POS) {
            collect_begin(4);   /* dx, dy, ang, cls */
        } else {
            collect_begin(2);   /* dx, dy */
        }
        return;
    }

    /* ---- "end" — 位置数据序列结束 ---- */
    if (str[0] == 'e' && str[1] == 'n' && str[2] == 'd' && str[3] == '\0') {
        if (!g_collecting) return;
        g_collecting = false;

        if (g_p1_sub == P1_S1_WAIT_POS) {
            g_result.dx         = g_tmp_dx;
            g_result.dy         = g_tmp_dy;
            g_result.angle_x100 = g_tmp_ang;
            safe_strcpy(g_result.class_name, g_tmp_cls, sizeof(g_result.class_name));
            g_state = VISION_GOT_POS;
        } else {
            g_result.dx         = g_tmp_dx;
            g_result.dy         = g_tmp_dy;
            g_result.angle_x100 = 0;
            g_result.class_name[0] = '\0';
            g_state = VISION_GOT_POS;
        }
        return;
    }

    /* ---- 收集模式：处理数据字段 ---- */
    if (g_collecting) {
        int32_t val;
        if (parse_number_frame(str, &val)) {
            switch (g_collect_cnt) {
            case 0: g_tmp_dx  = val; break;
            case 1: g_tmp_dy  = val; break;
            case 2: g_tmp_ang = val; break;
            default: break;
            }
        } else {
            if (g_collect_cnt == 3 && g_p1_sub == P1_S1_WAIT_POS) {
                safe_strcpy(g_tmp_cls, str, sizeof(g_tmp_cls));
            }
        }
        g_collect_cnt++;
    }
}

/* ================================================================
 *  Process2 帧分发 (ISR 上下文)
 *
 *  关键设计：
 *  - 非末 Mark 收到 "ok" → 切换到 P2_WAIT_GO + VISION_GOT_STOP
 *    宿主调 Vision_Go() → P2_SEARCHING → 搜索下一个 Mark
 *  - 末 Mark 收到 "ok" → VISION_DONE (全部完成)
 *  - "stp" 仅在 P2_SEARCHING 时有效 → VISION_GOT_STOP
 * ================================================================ */
static void process_p2_frame(const char *str) {
    /* ---- 错误帧 ---- */
    if (str[0] == 'e' && str[1] == 'r' && str[2] == 'r') {
        g_collecting = false;
        g_state = VISION_ERROR;
        save_error(str);
        return;
    }

    /* ---- "stp" — Mark 已锁定 ---- */
    if (str[0] == 's' && str[1] == 't' && str[2] == 'p' && str[3] == '\0') {
        g_collecting = false;
        if (g_p2_sub == P2_SEARCHING) {
            g_result.mark_index = g_p2_mark_idx;
            g_result.mark_count = P2_MARK_COUNT;
            g_state = VISION_GOT_STOP;
        }
        return;
    }

    /* ---- "ok" — 对齐完成 或 全部完成 ---- */
    if (str[0] == 'o' && str[1] == 'k' && str[2] == '\0') {
        g_collecting = false;
        if (g_p2_sub == P2_ALIGNING) {
            g_p2_mark_idx++;
            g_result.mark_index = g_p2_mark_idx;
            g_result.mark_count = P2_MARK_COUNT;

            if (g_p2_mark_idx >= P2_MARK_COUNT) {
                /* 全部 3 个 Mark 完成 */
                g_state = VISION_DONE;
            } else {
                /* 非末 Mark: 切到 P2_WAIT_GO，等宿主发 "go" 搜索下一个 */
                g_p2_sub = P2_WAIT_GO;
                g_state = VISION_GOT_STOP;
            }
        }
        return;
    }

    /* ---- "pos" — 开始 Mark 偏移数据 ---- */
    if (str[0] == 'p' && str[1] == 'o' && str[2] == 's' && str[3] == '\0') {
        if (g_p2_sub == P2_POS_DETECT) {
            collect_begin(2);   /* N:dx_mm10000, N:dy_mm10000 */
        }
        return;
    }

    /* ---- "end" — Mark 偏移数据结束 ---- */
    if (str[0] == 'e' && str[1] == 'n' && str[2] == 'd' && str[3] == '\0') {
        if (!g_collecting) return;
        g_collecting = false;

        if (g_p2_sub == P2_POS_DETECT) {
            g_result.dx         = g_tmp_dx;
            g_result.dy         = g_tmp_dy;
            g_result.mark_index = g_p2_mark_idx;
            g_result.mark_count = P2_MARK_COUNT;
            g_result.angle_x100 = 0;
            g_result.class_name[0] = '\0';
            g_state = VISION_GOT_POS;
        }
        return;
    }

    /* ---- 收集模式：处理 "N:xxx" 数据字段 ---- */
    if (g_collecting) {
        int32_t val;
        if (parse_number_frame(str, &val)) {
            switch (g_collect_cnt) {
            case 0: g_tmp_dx  = val; break;
            case 1: g_tmp_dy  = val; break;
            default: break;
            }
        }
        g_collect_cnt++;
    }
}

/* ================================================================
 *  Process3 帧分发 (ISR 上下文)
 *
 *  两阶段，无 "stp":
 *   Phase 1: 自动检测初始偏移 → "pos" N:dx N:dy "end"
 *   Phase 2: 迭代修正 (最多 5 轮, 对齐阈值 3px) → iter pos 或 "ok"
 * ================================================================ */
static void process_p3_frame(const char *str) {
    /* ---- 错误帧 ---- */
    if (str[0] == 'e' && str[1] == 'r' && str[2] == 'r') {
        g_collecting = false;
        g_state = VISION_ERROR;
        save_error(str);
        return;
    }

    /* ---- "ok" — 对齐完成 ---- */
    if (str[0] == 'o' && str[1] == 'k' && str[2] == '\0') {
        g_collecting = false;
        g_state = VISION_DONE;
        return;
    }

    /* ---- "pos" — 开始位置数据序列 ---- */
    if (str[0] == 'p' && str[1] == 'o' && str[2] == 's' && str[3] == '\0') {
        collect_begin(2);   /* N:dx, N:dy */
        return;
    }

    /* ---- "end" — 位置数据序列结束 ---- */
    if (str[0] == 'e' && str[1] == 'n' && str[2] == 'd' && str[3] == '\0') {
        if (!g_collecting) return;
        g_collecting = false;

        g_result.dx         = g_tmp_dx;
        g_result.dy         = g_tmp_dy;
        g_result.angle_x100 = 0;
        g_result.class_name[0] = '\0';
        g_state = VISION_GOT_POS;
        return;
    }

    /* ---- 收集模式：处理 "N:xxx" 数据字段 ---- */
    if (g_collecting) {
        int32_t val;
        if (parse_number_frame(str, &val)) {
            switch (g_collect_cnt) {
            case 0: g_tmp_dx  = val; break;
            case 1: g_tmp_dy  = val; break;
            default: break;
            }
        }
        g_collect_cnt++;
    }
}

/* ================================================================
 *  帧分发入口 (ISR 上下文)
 * ================================================================ */
static void process_frame(const char *str) {
    if (g_state == VISION_IDLE) return;

    switch (g_active_cmd) {
    case VCMD_P1: process_p1_frame(str); break;
    case VCMD_P2: process_p2_frame(str); break;
    case VCMD_P3: process_p3_frame(str); break;
    default: break;
    }
}

/* ================================================================
 *  帧解析器：逐字节喂入 (ISR 上下文)
 * ================================================================ */
static void feed_byte(uint8_t byte) {
    switch (g_fs) {

    case FS_WAIT_HEAD:
        if (byte == FRAME_HEAD) {
            g_fs        = FS_WAIT_LEN;
            g_frame_idx = 0;
            g_frame_len = 0;
        }
        break;

    case FS_WAIT_LEN:
        g_frame_len = byte;
        if (g_frame_len == 0) {
            g_fs = FS_WAIT_TAIL;
        } else if (g_frame_len > FRAME_PAYLOAD_MAX) {
            g_fs = FS_WAIT_HEAD;
        } else {
            g_fs = FS_WAIT_DATA;
        }
        break;

    case FS_WAIT_DATA:
        g_frame_buf[g_frame_idx++] = byte;
        if (g_frame_idx >= g_frame_len) {
            g_fs = FS_WAIT_TAIL;
        }
        break;

    case FS_WAIT_TAIL:
        if (byte == FRAME_TAIL) {
            g_frame_buf[g_frame_len] = '\0';
            process_frame((const char *)g_frame_buf);
        }
        g_fs = FS_WAIT_HEAD;
        break;
    }
}

/* ================================================================
 *  公共 API (任务上下文，可调用 PrintDebug)
 * ================================================================ */

void Vision_Init(void) {
    g_fs = FS_WAIT_HEAD;
    reset_all();
    PrintDebug("[VISION] Init done (v2 protocol).\r\n");
}

void Vision_Start(VisionCmd_t cmd) {
    reset_all();
    g_active_cmd = cmd;

    switch (cmd) {
    case VCMD_P1:
        g_p1_sub  = P1_S0_WAIT_STOP;
        g_p1_iter = 0;
        g_state   = VISION_BUSY;
        send_frame("p1");
        PrintDebug("[VISION] -> Cam: p1 (P1 start)\r\n");
        break;

    case VCMD_P2:
        g_p2_sub      = P2_IDLE;
        g_p2_mark_idx = 0;
        g_state       = VISION_IDLE;
        send_frame("p2");
        PrintDebug("[VISION] -> Cam: p2 (P2 start, %d marks)\r\n", P2_MARK_COUNT);
        break;

    case VCMD_P3:
        g_p3_sub  = P3_PHASE1;
        g_p3_iter = 0;
        g_state   = VISION_BUSY;
        send_frame("p3");
        PrintDebug("[VISION] -> Cam: p3 (P3 start)\r\n");
        break;

    default:
        g_state = VISION_IDLE;
        break;
    }
}

void Vision_Go(void) {
    /* P2 首次 go: IDLE → 发 "go" 开始搜索 */
    if (g_active_cmd == VCMD_P2 && g_state == VISION_IDLE && g_p2_sub == P2_IDLE) {
        send_frame("go");
        g_p2_sub = P2_SEARCHING;
        g_state  = VISION_BUSY;
        PrintDebug("[VISION] -> Cam: go (P2 search mark %d)\r\n", g_p2_mark_idx);
        return;
    }

    /* 其他 Process: 仅在 GOT_STOP 或 GOT_POS 时允许 */
    if (g_state != VISION_GOT_STOP && g_state != VISION_GOT_POS) {
        return;
    }

    send_frame("go");

    if (g_active_cmd == VCMD_P1) {
        if (g_p1_sub == P1_S0_WAIT_STOP) {
            g_p1_sub = P1_S1_WAIT_POS;
            PrintDebug("[VISION] -> Cam: go (P1 Phase1)\r\n");
        } else if (g_p1_sub == P1_S1_WAIT_POS) {
            g_p1_sub  = P1_S2_ITERATING;
            g_p1_iter = 0;
            PrintDebug("[VISION] -> Cam: go (P1 Phase2 iter=0)\r\n");
        } else if (g_p1_sub == P1_S2_ITERATING) {
            g_p1_iter++;
            PrintDebug("[VISION] -> Cam: go (P1 iter=%d)\r\n", g_p1_iter);
        }
    }
    else if (g_active_cmd == VCMD_P2) {
        if (g_p2_sub == P2_WAIT_GO) {
            /* 中间 "ok" 后 → 发 "go" 搜索下一个 Mark */
            g_p2_sub = P2_SEARCHING;
            PrintDebug("[VISION] -> Cam: go (P2 search mark %d)\r\n", g_p2_mark_idx);
        } else if (g_p2_sub == P2_SEARCHING) {
            /* stp → go: 进入精确测位 */
            g_p2_sub = P2_POS_DETECT;
            PrintDebug("[VISION] -> Cam: go (P2 pos-detect mark %d)\r\n", g_p2_mark_idx);
        } else if (g_p2_sub == P2_POS_DETECT) {
            /* pos → go: 进入迭代归零 */
            g_p2_sub = P2_ALIGNING;
            PrintDebug("[VISION] -> Cam: go (P2 aligning mark %d)\r\n", g_p2_mark_idx);
        }
    }
    else if (g_active_cmd == VCMD_P3) {
        if (g_p3_sub == P3_PHASE1) {
            g_p3_sub  = P3_PHASE2;
            g_p3_iter = 0;
            PrintDebug("[VISION] -> Cam: go (P3 Phase2 iter=0)\r\n");
        } else if (g_p3_sub == P3_PHASE2) {
            g_p3_iter++;
            PrintDebug("[VISION] -> Cam: go (P3 iter=%d)\r\n", g_p3_iter);
        }
    }

    g_state = VISION_BUSY;
}

VisionState_t Vision_GetState(void) {
    return g_state;
}

const VisionResult_t* Vision_GetResult(void) {
    return &g_result;
}

const char* Vision_GetError(void) {
    return g_error_code;
}

/* ================================================================
 *  UART 回调入口 (ISR 上下文 — driver_uart.c 中
 *  HAL_UARTEx_RxEventCallback 调用)
 * ================================================================ */
void CamUart_RecvCallback(uint8_t *data, int len) {
    for (int i = 0; i < len; i++) {
        feed_byte(data[i]);
    }
}

/* ================================================================
 *  旧协议兼容 API (逐步废弃)
 * ================================================================ */
void Vision_SendCmd(CamCmd_t cmd) {
    switch (cmd) {
    case CAM_CMD_PROC1: Vision_Start(VCMD_P1); break;
    case CAM_CMD_PROC2: Vision_Start(VCMD_P2); break;
    case CAM_CMD_PROC3: Vision_Start(VCMD_P3); break;
    default: break;
    }
}
