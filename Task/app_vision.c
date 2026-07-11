#include "app_vision.h"
#include "app_host.h"
#include "app_test.h"
#include "driver_uart.h"
#include <string.h>

/* ================================================================
 *  Frame protocol constants (MaixCAM 2026 v2)
 *  Format: 0x7E | LEN(1B) | PAYLOAD(LEN bytes, UTF-8) | 0x7F
 * ================================================================ */
#define FRAME_HEAD         0x7E
#define FRAME_TAIL         0x7F
#define FRAME_PAYLOAD_MAX  255

/* ================================================================
 *  Frame parser states
 * ================================================================ */
typedef enum {
    FS_WAIT_HEAD,
    FS_WAIT_LEN,
    FS_WAIT_DATA,
    FS_WAIT_TAIL,
} FrameState_t;

/* ================================================================
 *  Process1 internal sub-states
 *
 *  P1_S_CATEGORY   -> sent "p1", waiting for "rdy" (category query)
 *  P1_S0_WAIT_STOP -> sent cls+N:id+end, waiting for "stp" (Phase0)
 *  P1_S1_WAIT_POS  -> sent "go", waiting for initial pos (Phase1: dx,dy,id)
 *  P1_S2_ITERATING -> sent "go", waiting for iter pos or "ok" (Phase2)
 *  P1_S_WAIT_ANGLE -> received "ok", waiting for N:{ao} angle frame
 * ================================================================ */
typedef enum {
    P1_S_CATEGORY,       /* waiting for "rdy" after "p1" */
    P1_S0_WAIT_STOP,     /* Phase 0: waiting for "stp" */
    P1_S1_WAIT_POS,      /* Phase 1: waiting for initial pos */
    P1_S2_ITERATING,     /* Phase 2: iterative alignment */
    P1_S_WAIT_ANGLE,     /* waiting for angle frame after "ok" */
} P1_State_t;

/* ================================================================
 *  Process2 internal sub-states
 * ================================================================ */
typedef enum {
    P2_WAIT_RDY,    /* waiting for "rdy" after "p2" */
    P2_IDLE,
    P2_WAIT_GO,
    P2_SEARCHING,
    P2_POS_DETECT,
    P2_ALIGNING,
} P2_State_t;

/* ================================================================
 *  Process3 内部子状态
 * ================================================================ */
typedef enum {
    P3_PHASE1,
    P3_PHASE2,
    P3_S_WAIT_ANGLE,
} P3_State_t;

/* ================================================================
 *  全局状态
 * ================================================================ */

/* ---- 帧解析器 ---- */
static FrameState_t  g_fs           = FS_WAIT_HEAD;
static uint8_t       g_frame_len    = 0;
static uint8_t       g_frame_idx    = 0;
static uint8_t       g_frame_buf[FRAME_PAYLOAD_MAX + 1];

/* ---- 对外状态 ---- */
static volatile VisionState_t g_state = VISION_IDLE;  /* ISR写入，必须volatile */
static VisionCmd_t   g_active_cmd   = VCMD_P1;

/* ---- Process1 子状态 ---- */
static P1_State_t    g_p1_sub       = P1_S0_WAIT_STOP;
static int           g_p1_iter      = 0;
static int           g_p1_class_id  = -1;

/* ---- Process2 子状态 ---- */
static P2_State_t    g_p2_sub       = P2_IDLE;
static int           g_p2_mark_idx  = 0;    /* 当前 Mark 序号 (0-based) */

/* ---- Process3 子状态 ---- */
static P3_State_t    g_p3_sub       = P3_PHASE1;
static int           g_p3_iter      = 0;
static int           g_p3_retry_cnt = 0;
static int           g_p2_iter      = 0;  /* P2 aligning 迭代计数 */

/* ---- 字段收集状态 ---- */
static bool          g_collecting       = false;
static int           g_collect_cnt      = 0;
static int           g_collect_exp      = 0;
static bool          g_collect_auto_end = false;

/* ---- 收集暂存 ---- */
static int32_t       g_tmp_dx       = 0;
static int32_t       g_tmp_dy       = 0;
static int32_t       g_tmp_cls      = 0;

/* ---- 超时保护 ---- */
#define VISION_TIMEOUT_MS  120000   /* 2 分钟，P2 扫描+3 个 Mark 建系 */
static uint32_t g_vision_start_tick = 0;

/* ---- 结果与错误 ---- */
static VisionResult_t g_result;
static char           g_error_code[8];
static volatile uint32_t g_p2_align_rx_cnt = 0;  /* ISR-safe: P2 aligning 收帧计数 */
static volatile int      g_p2_got_pos_from_isr = 0;  /* ISR set GOT_POS flag */
static volatile uint32_t g_p2_total_rx = 0;           /* ISR-safe: P2 任意帧总计数 */
static volatile uint32_t g_p2_stp_ignored = 0;       /* ISR-safe: P2 stp 被忽略次数 */

/* ---- P0 handshake state ---- */
static bool          g_p0_done      = false;

/* ================================================================
 *  Forward declarations
 * ================================================================ */
static void feed_byte(uint8_t byte);
static void process_frame(const char *str);

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

static bool parse_number_frame(const char *str, int32_t *out) {
    if (str[0] != (char)0x4E || str[1] != (char)0x3A) return false;
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

static void send_frame(const char *str) {
    uint8_t len = (uint8_t)strlen(str);
    if (len > FRAME_PAYLOAD_MAX) len = FRAME_PAYLOAD_MAX;
    uint8_t buf[FRAME_PAYLOAD_MAX + 3];
    buf[0] = FRAME_HEAD;
    buf[1] = len;
    if (len > 0) memcpy(buf + 2, str, len);
    buf[2 + len] = FRAME_TAIL;
    if (HAL_UART_Transmit(&huart2, buf, (uint16_t)(len + 3), 100) != HAL_OK) {
        PrintDebug("[VISION] send_frame FAILED: '%s'\r\n", str);
    } else if (str[0] == 'g' && str[1] == 'o' && str[2] == '\0') {
        PrintDebug("[VISION] send_frame OK: 'go' sent, RX_DMA active=%d\r\n",
                   (int)(READ_BIT(huart2.Instance->CR1, USART_CR1_RE) ? 1 : 0));
    }
}

/* 重置所有内部状态 */
static void reset_all(void) {
    g_fs = FS_WAIT_HEAD;
    g_state        = VISION_IDLE;
    g_active_cmd   = VCMD_P1;
    g_p1_sub       = P1_S0_WAIT_STOP;
    g_p1_iter      = 0;
    g_p1_class_id  = -1;
    g_p2_sub       = P2_IDLE;
    g_p2_mark_idx  = 0;
    g_p3_sub       = P3_PHASE1;
    g_p3_iter      = 0;
    g_p3_retry_cnt = 0;
    g_p2_iter      = 0;
    g_p2_total_rx  = 0;
    g_p2_stp_ignored = 0;
    g_collecting    = false;
    g_collect_cnt   = 0;
    g_collect_auto_end = false;
    memset(&g_result, 0, sizeof(g_result));
    memset(g_error_code, 0, sizeof(g_error_code));
}

static void collect_begin(int expected, bool auto_end) {
    g_collecting       = true;
    g_collect_cnt      = 0;
    g_collect_exp      = expected;
    g_collect_auto_end = auto_end;
    g_tmp_dx  = 0;
    g_tmp_dy  = 0;
    g_tmp_cls = 0;
}

/* 安全拷贝字符串到固定大小 buffer */
static void safe_strcpy(char *dst, const char *src, int dst_size) {
    int i = 0;
    while (src[i] && i < dst_size - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* 安全拷贝错误码 */
static void save_error(const char *str) {
    safe_strcpy(g_error_code, str, sizeof(g_error_code));
}

static const char* class_id_to_name(int id) {
    switch (id) {
    case 0: return "ccapt";
    case 1: return "cledy";
    case 2: return "cledo";
    case 3: return "crest";
    default: return "?";
    }
}

static void fill_class_id(int32_t id) {
    g_result.class_id = id;
    safe_strcpy(g_result.class_name, class_id_to_name((int)id), sizeof(g_result.class_name));
}

/* ================================================================
 *  Process1 帧分发 (ISR context)
 *
 *  v2 changes:
 *  - Category query: "p1" -> wait "rdy" -> send cls N:id end
 *  - Phase1 pos: 3 fields (dx, dy, class_id), no angle
 *  - Phase2 ok: followed by N:{ao} angle frame
 * ================================================================ */
static void process_p1_frame(const char *str) {
    /* ---- 错误帧优先级最高 ---- */
    if (str[0] == 'e' && str[1] == 'r' && str[2] == 'r') {
        g_collecting = false;
        g_state = VISION_ERROR;
        save_error(str);
        return;
    }

    /* ---- Category query: waiting for "rdy" ---- */
    if (g_p1_sub == P1_S_CATEGORY) {
        if (str[0] == 'r' && str[1] == 'd' && str[2] == 'y' && str[3] == '\0') {
            /* Cam asks which category.
             * Defer the reply to task context (Vision_ClsReply).
             * We CANNOT send UART frames from ISR context. */
            g_state = VISION_GOT_CATEGORY_QUERY;
        }
        return;
    }

    /* ---- "stp" ---- */
    if (str[0] == 's' && str[1] == 't' && str[2] == 'p' && str[3] == '\0') {
        g_collecting = false;
        if (g_p1_sub == P1_S0_WAIT_STOP) g_state = VISION_GOT_STOP;
        return;
    }

    /* ---- "ok" ---- */
    if (str[0] == 'o' && str[1] == 'k' && str[2] == '\0') {
        g_collecting = false;
        if (g_p1_sub == P1_S2_ITERATING) {
            g_p1_sub = P1_S_WAIT_ANGLE;   /* wait for angle next */
        }
        return;
    }

    /* ---- Angle frame after "ok" ---- */
    if (g_p1_sub == P1_S_WAIT_ANGLE) {
        int32_t val;
        if (parse_number_frame(str, &val)) {
            g_result.angle_x100  = val;
            g_result.angle_valid = true;
            g_state = VISION_DONE;
        }
        return;
    }

    /* ---- "pos" — 开始位置数据序列 ---- */
    if (str[0] == 'p' && str[1] == 'o' && str[2] == 's' && str[3] == '\0') {
        if (g_p1_sub == P1_S1_WAIT_POS)
            collect_begin(3, false);   /* dx, dy, class_id */
        else
            collect_begin(2, false);   /* dx, dy */
        return;
    }

    /* ---- "end" — 位置数据序列结束 ---- */
    if (str[0] == 'e' && str[1] == 'n' && str[2] == 'd' && str[3] == '\0') {
        if (!g_collecting) return;
        g_collecting = false;

        if (g_p1_sub == P1_S1_WAIT_POS) {
            g_result.dx         = g_tmp_dx;
            g_result.dy         = g_tmp_dy;
            g_result.angle_x100 = 0;
            g_result.angle_valid = false;
            fill_class_id(g_tmp_cls);
            g_state = VISION_GOT_POS;
        } else {
            g_result.dx         = g_tmp_dx;
            g_result.dy         = g_tmp_dy;
            g_result.angle_x100 = 0;
            g_result.angle_valid = false;
            g_result.class_id   = -1;
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
            case 2: g_tmp_cls = val; break;
            default: break;
            }
        }
        g_collect_cnt++;
    }
}

/* ================================================================
 *  Process2 frame dispatch (ISR context)
 * ================================================================ */
static void process_p2_frame(const char *str) {
    g_p2_total_rx++;  /* 统计 P2 收到的任意帧 */
    /* ---- 错误帧 ---- */
    if (str[0] == 'e' && str[1] == 'r' && str[2] == 'r') {
        g_collecting = false;
        g_state = VISION_ERROR;
        save_error(str);
        return;
    }

    /* ---- "rdy" — Cam ready after "p2" ---- */
    if (str[0] == 'r' && str[1] == 'd' && str[2] == 'y' && str[3] == '\0') {
        if (g_p2_sub == P2_WAIT_RDY) {
            g_state = VISION_RDY;
        }
        return;
    }

    /* ---- "stp" — Mark 已锁定 ---- */
    if (str[0] == 's' && str[1] == 't' && str[2] == 'p' && str[3] == '\0') {
        if (g_p2_sub == P2_SEARCHING || g_p2_sub == P2_WAIT_RDY) {
            g_collecting = false;
            g_result.mark_index = g_p2_mark_idx;
            g_result.mark_count = P2_MARK_COUNT;
            g_state = VISION_GOT_STOP;
            g_p2_sub = P2_SEARCHING;
        } else {
            g_p2_stp_ignored++;  /* stp 在非搜索状态下被忽略 */
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
        if (g_p2_sub == P2_POS_DETECT || g_p2_sub == P2_ALIGNING) collect_begin(2, false);
        return;
    }

    if (g_p2_sub == P2_ALIGNING) g_p2_align_rx_cnt++;

    /* ---- "end" — Mark 偏移数据结束 ---- */
    if (str[0] == 'e' && str[1] == 'n' && str[2] == 'd' && str[3] == '\0') {
        if (!g_collecting) return;
        g_collecting = false;
        if (g_p2_sub == P2_POS_DETECT || g_p2_sub == P2_ALIGNING) {
            g_result.dx         = g_tmp_dx;
            g_result.dy         = g_tmp_dy;
            g_result.mark_index = g_p2_mark_idx;
            g_result.mark_count = P2_MARK_COUNT;
            g_result.angle_x100 = 0;
            g_result.angle_valid = false;
            g_result.class_id   = -1;
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
            case 0: g_tmp_dx = val; break;
            case 1: g_tmp_dy = val; break;
            default: break;
            }
        }
        g_collect_cnt++;
    }
}

/* ================================================================
 *  Process3 帧分发 (ISR 上下文)
 *
 *  v2 changes:
 *  - Phase1: pos N:dx N:dy WITH "end" (same format as Phase2)
 *  - Phase2: ok followed by N:{ao} angle frame
 *  - err3_3: non-fatal (Cam requests retry), err3_7: fatal
 * ================================================================ */
static void process_p3_frame(const char *str) {
    /* ---- Error: err3_3 is recoverable ---- */
    if (str[0] == 'e' && str[1] == 'r' && str[2] == 'r') {
        g_collecting = false;
        if (str[3] == '3' && str[4] == '_' && str[5] == '3' && str[6] == '\0') {
            g_state = VISION_GOT_ERR_RETRY;
            g_p3_retry_cnt++;
            return;
        }
        g_state = VISION_ERROR;
        save_error(str);
        return;
    }

    /* ---- Angle frame after "ok" ---- */
    if (g_p3_sub == P3_S_WAIT_ANGLE) {
        int32_t val;
        if (parse_number_frame(str, &val)) {
            g_result.angle_x100  = val;
            g_result.angle_valid = true;
            g_state = VISION_DONE;
        }
        return;
    }

    /* ---- "ok" ---- */
    if (str[0] == 'o' && str[1] == 'k' && str[2] == '\0') {
        g_collecting = false;
        if (g_p3_sub == P3_PHASE2) {
            g_p3_sub = P3_S_WAIT_ANGLE;   /* wait for angle next */
        }
        return;
    }

    /* ---- "pos" — 开始位置数据序列 ---- */
    if (str[0] == 'p' && str[1] == 'o' && str[2] == 's' && str[3] == '\0') {
        if (g_p3_sub == P3_PHASE1)
            collect_begin(2, false);   /* needs "end" */
        else
            collect_begin(2, false);   /* needs "end" */
        return;
    }

    /* ---- "end" (Phase1/Phase2) ---- */
    if (str[0] == 'e' && str[1] == 'n' && str[2] == 'd' && str[3] == '\0') {
        if (!g_collecting) return;
        g_collecting = false;
        if (g_p3_sub == P3_PHASE1 || g_p3_sub == P3_PHASE2) {
            g_result.dx         = g_tmp_dx;
            g_result.dy         = g_tmp_dy;
            g_result.angle_x100 = 0;
            g_result.angle_valid = false;
            g_result.class_id   = -1;
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
            case 0: g_tmp_dx = val; break;
            case 1: g_tmp_dy = val; break;
            default: break;
            }
        }
        g_collect_cnt++;

        /* P3 Phase1 auto-complete: 2 numbers -> done */
        if (g_collect_auto_end && g_collect_cnt >= g_collect_exp) {
            g_collecting = false;
            g_result.dx         = g_tmp_dx;
            g_result.dy         = g_tmp_dy;
            g_result.angle_x100 = 0;
            g_result.angle_valid = false;
            g_result.class_id   = -1;
            g_result.class_name[0] = '\0';
            g_state = VISION_GOT_POS;
        }
    }
}

/* ================================================================
 *  Frame dispatch: handles both P0 handshake and active processes
 * ================================================================ */
static void process_frame(const char *str) {
    /* During P0 handshake or idle: check for handshake frames */
    if (g_state == VISION_IDLE) {
        if (str[0] == 'r' && str[1] == 'd' && str[2] == 'y' && str[3] == '\0') {
            g_p0_done = true;
        }
        if (str[0] == 'e' && str[1] == 'r' && str[2] == 'r' && str[3] == '0' && str[4] == '\0') {
            g_state = VISION_ERROR;
            save_error(str);
        }
        /* err0: Cam timed out, host should retry */
        return;
    }

    switch (g_active_cmd) {
    case VCMD_P1: process_p1_frame(str); break;
    case VCMD_P2: process_p2_frame(str); break;
    case VCMD_P3: process_p3_frame(str); break;
    default: break;
    }
}

/* ================================================================
 *  Frame parser: feed bytes one at a time
 * ================================================================ */
static void feed_byte(uint8_t byte) {
    switch (g_fs) {
    case FS_WAIT_HEAD:
        if (byte == FRAME_HEAD) {
            g_fs = FS_WAIT_LEN;
            g_frame_idx = 0;
            g_frame_len = 0;
        }
        break;

    case FS_WAIT_LEN:
        g_frame_len = byte;
        if (g_frame_len == 0)
            g_fs = FS_WAIT_TAIL;
        else if (g_frame_len > FRAME_PAYLOAD_MAX)
            g_fs = FS_WAIT_HEAD;
        else
            g_fs = FS_WAIT_DATA;
        break;

    case FS_WAIT_DATA:
        g_frame_buf[g_frame_idx++] = byte;
        if (g_frame_idx >= g_frame_len)
            g_fs = FS_WAIT_TAIL;
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
    g_p0_done = false;
    reset_all();
    PrintDebug("[VISION] Init done (v2 protocol).\r\n");
}

bool Vision_Handshake(uint32_t timeout_ms) {
    PrintDebug("[VISION] P0 handshake: sending p0...\r\n");
    g_p0_done = false;
    g_state   = VISION_IDLE;
    g_fs      = FS_WAIT_HEAD;

    send_frame("p0");

    /* ISR path (CamUart_RecvCallback -> feed_byte -> process_frame)
     * handles byte feeding and sets g_p0_done when "rdy" arrives.
     * We only need to keep DMA flowing and poll the flag. */
    uint32_t start = osKernelGetTickCount();
    while ((osKernelGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        UART_Driver_Process();

        if (g_p0_done) {
            PrintDebug("[VISION] P0 handshake: rdy received, done.\r\n");
            return true;
        }

        if (g_state == VISION_ERROR) {
            PrintDebug("[VISION] P0 handshake: err0 received, abort.\r\n");
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    PrintDebug("[VISION] P0 handshake: TIMEOUT!\r\n");
    return false;
}

void Vision_Start(VisionCmd_t cmd, int class_id) {
    reset_all();
    g_vision_start_tick = osKernelGetTickCount();
    g_active_cmd = cmd;

    switch (cmd) {
    case VCMD_P1:
        g_p1_class_id = class_id;
        g_p1_sub      = P1_S_CATEGORY;
        g_p1_iter     = 0;
        g_state       = VISION_BUSY;
        send_frame("p1");
        PrintDebug("[VISION] -> Cam: p1 (P1 start, class=%d)\r\n", class_id);
        break;

    case VCMD_P2:
        g_p2_sub      = P2_WAIT_RDY;
        g_p2_mark_idx = 0;
        g_state       = VISION_BUSY;
        send_frame("p2");
        PrintDebug("[VISION] -> Cam: p2 (P2 start, %d marks)\r\n", P2_MARK_COUNT);
        break;

    case VCMD_P3:
        g_p3_sub       = P3_PHASE1;
        g_p3_iter      = 0;
        g_p3_retry_cnt = 0;
        g_state        = VISION_BUSY;
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

    /* P2: rdy received, send go to start search */
    if (g_state == VISION_RDY && g_p2_sub == P2_WAIT_RDY) {
        send_frame("go");
        g_p2_sub = P2_SEARCHING;
        g_state  = VISION_BUSY;
        PrintDebug("[VISION] -> Cam: go (P2 start search, rdy received)\r\n");
        return;
    }

    /* err3_3 retry: resend go */
    if (g_state == VISION_GOT_ERR_RETRY) {
        send_frame("go");
        g_state = VISION_BUSY;
        PrintDebug("[VISION] -> Cam: go (P3 err3_3 retry #%d)\r\n", g_p3_retry_cnt);
        return;
    }

    if (g_state != VISION_GOT_STOP && g_state != VISION_GOT_POS) return;

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
            g_p2_iter = 0;
            PrintDebug("[VISION] -> Cam: go (P2 aligning mark %d), waiting...\r\n", g_p2_mark_idx);
        } else if (g_p2_sub == P2_ALIGNING) {
            /* 迭代对齐: 发 go 继续迭代, 最多5轮 */
            g_p2_iter++;
            PrintDebug("[VISION] -> Cam: go (P2 align iter %d, mark %d)\r\n", g_p2_iter, g_p2_mark_idx);
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

/* P1 category reply: send cls + N:{id} + end (MUST be called from task context) */
void Vision_ClsReply(void) {
    if (g_state != VISION_GOT_CATEGORY_QUERY) return;
    if (g_active_cmd != VCMD_P1) return;

    send_frame("cls");

    char numbuf[14];
    int n = 0;
    int id = g_p1_class_id;
    numbuf[n++] = 'N';
    numbuf[n++] = ':';
    if (id < 0) { numbuf[n++] = '-'; id = -id; }
    char rev[6]; int ri = 0;
    int tmp = id;
    do { rev[ri++] = (char)('0' + (tmp % 10)); tmp /= 10; } while (tmp);
    while (ri--) numbuf[n++] = rev[ri];
    numbuf[n] = '\0';
    send_frame(numbuf);
    send_frame("end");

    g_p1_sub = P1_S0_WAIT_STOP;
    g_state  = VISION_BUSY;
    PrintDebug("[VISION] -> Cam: cls=%d (category reply)\r\n", (int)g_p1_class_id);
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

const char* Vision_ClassName(int class_id) {
    return class_id_to_name(class_id);
}

/* ================================================================
 *  UART 回调入口 (ISR 上下文 — driver_uart.c 中
 *  HAL_UARTEx_RxEventCallback 调用)
 * ================================================================ */
void CamUart_RecvCallback(uint8_t *data, int len) {
    for (int i = 0; i < len; i++) {
        feed_byte(data[i]);
    }
    /* ISR 路径尾: 若刚转为 GOT_POS 则标记 */
    if (g_active_cmd == VCMD_P2 && g_p2_sub == P2_ALIGNING && g_state == VISION_GOT_POS) {
        g_p2_got_pos_from_isr = 1;
    }
}

bool Vision_IsTimedOut(void) {
    return (osKernelGetTickCount() - g_vision_start_tick) >= pdMS_TO_TICKS(VISION_TIMEOUT_MS);
}

void Vision_ForceIdle(void) {
    PrintDebug("[VISION] Force idle due to timeout\r\n");
    reset_all();
}

void Vision_SendEnd(void) {
    send_frame("end");
}

uint32_t Vision_GetAlignRxCount(void) { return g_p2_align_rx_cnt; }
int Vision_GetGotPosFromISR(void) { return g_p2_got_pos_from_isr; }
uint32_t Vision_GetP2TotalRxCount(void) { return g_p2_total_rx; }
uint32_t Vision_GetP2StpIgnoredCount(void) { return g_p2_stp_ignored; }

void Vision_BackToSearch(void) {
    if (g_active_cmd == VCMD_P2) {
        g_p2_sub = P2_SEARCHING;
        g_state = VISION_BUSY;
        g_collecting = false;
        PrintDebug("[VISION] Back to search mode (no p2 sent)\r\n");
    }
}
