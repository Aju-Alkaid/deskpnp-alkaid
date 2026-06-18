import re, os

fpath = r"E:\Desktop\qiansai\pnp_1\Task\app_host.c"

with open(fpath, "r", encoding="utf-8") as f:
    content = f.read()

# Verify we have valid content
print(f"Read {len(content)} bytes, {content.count(chr(10))} lines")

# === USER CHANGES ===

# 1. MAX_MARKS after MAX_COMPONENTS
content = content.replace(
    "#define MAX_COMPONENTS  128",
    '#define MAX_COMPONENTS  128\n#define MAX_MARKS       8     /* Mark \u70b9\u6700\u5927\u6570\u91cf\uff08\u89c4\u8303\u4e3a 5\uff09 */'
)

# 2. Add footprint/layer/is_mark to Component_t
old_comp = "    uint8_t feeder_id;\n    bool placed;"
new_comp = (
'    char     footprint[32];   /* \u5c01\u88c5\u540d\u79f0 (C0805, R0805, LED-SMD, Mark1~5) */\n'
'    char     layer;           /* \u5c42\u9762 \'T\' \u6216 \'B\' */\n'
'    bool     is_mark;         /* SMD=="MARK" \u65f6\u4e3a true */\n'
'    uint8_t feeder_id;\n'
'    bool placed;'
)
content = content.replace(old_comp, new_comp)

# 3. Add g_marks + g_mark_count
content = content.replace(
    "static uint16_t     g_comp_index = 0;",
    "static uint16_t     g_comp_index = 0;\n\nstatic Component_t  g_marks[MAX_MARKS];\nstatic uint16_t     g_mark_count = 0;"
)

# 4. Add g_mark_count=0 in Host_Task init
content = content.replace(
    "    g_comp_count = 0;\n    g_comp_index = 0;",
    "    g_comp_count = 0;\n    g_comp_index = 0;\n    g_mark_count = 0;"
)

# 5. Replace host_send (add HEATER include + app_config include)
content = content.replace(
    '#include "driver_tmc2209.h"',
    '#include "driver_tmc2209.h"\n#include "driver_heater.h"'
)
content = content.replace(
    '#include "app_host.h"\n#include "app_test.h"',
    '#include "app_host.h"\n#include "app_test.h"\n#include "app_config.h"       /* STEPS_PER_MM, X1_ADDR, X2_ADDR, Y_ADDR, JOG_MAX_STEPS */'
)

# 6. Remove old CSV functions (parse_header, get_csv_field) and replace parse_csv_line
# Remove parse_header
content = re.sub(
    r'/\* ---- CSV 解析.*?\*/\s*static void parse_header.*?\n\}',
    '',
    content,
    flags=re.DOTALL
)

# Remove get_csv_field
content = re.sub(
    r'static bool get_csv_field.*?\n\}',
    '',
    content,
    flags=re.DOTALL
)

# Remove old comment remnants and add new CSV section
new_csv_section = '''/* ---- CSV 解析（v2：制表符分隔、15 固定列、引号包裹、mm 后缀）---- */

/*
 * 提取第 n 个制表符分隔字段，去除首尾引号，写入 out
 * 返回 false 表示字段不存在
 */
static bool csv_get_field(const char *line, uint16_t len, int n,
                          char *out, uint16_t out_max) {
    if (n < 0) return false;
    const char *p = line;
    const char *end = line + len;
    int col = 0;

    /* 跳过前 n 个制表符 */
    while (col < n && p < end) {
        p = memchr(p, '\\t', (uint16_t)(end - p));
        if (p == NULL) return false;
        p++;
        col++;
    }
    if (p >= end) return false;

    /* 找到下一个制表符或行尾 */
    const char *next = memchr(p, '\\t', (uint16_t)(end - p));
    uint16_t flen = next ? (uint16_t)(next - p) : (uint16_t)(end - p);

    /* 去除首尾引号 */
    if (flen >= 2 && p[0] == '"' && p[flen - 1] == '"') {
        p++;
        flen -= 2;
    }

    if (flen >= out_max) flen = out_max - 1;
    memcpy(out, p, flen);
    out[flen] = '\\0';
    return true;
}

/*
 * 解析 "8mm" → 8.0f。去除末尾 "mm" 后调用 strtof
 */
static float parse_mm(const char *str) {
    char buf[32];
    uint16_t len = (uint16_t)strlen(str);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, str, len);
    buf[len] = '\\0';

    if (len >= 2 && buf[len - 2] == 'm' && buf[len - 1] == 'm') {
        buf[len - 2] = '\\0';
    }
    return strtof(buf, NULL);
}

'''

# Insert after host_send function
content = content.replace(
    "}\n\n/* ---- CSV 解析",
    "}\n\n" + new_csv_section + "/* ---- CSV 解析"
)

# Now find and remove leftover CSV comment line and old parse_csv_line
# The old parse_csv_line starts with: static bool parse_csv_line
old_parse = r'/\* ---- CSV 解析.*?\*/\s*\n\s*static bool parse_csv_line.*?\n\}'
content = re.sub(old_parse, '', content, flags=re.DOTALL)

# Insert new parse_csv_line before download_done
new_parse_csv = '''/*
 * 解析一行 CSV（15 列，制表符分隔）
 * 列结构固定：
 *   [0]Designator  [1]Device  [2]Footprint  [3]Mid X  [4]Mid Y
 *   [5]Ref X  [6]Ref Y  [7]Pad X  [8]Pad Y  [9]Pins
 *   [10]Layer  [11]Rotation  [12]SMD  [13]Comment  [14]Name
 *
 * 返回 true 表示该行被保留（存入 g_components 或 g_marks）
 */
static bool parse_csv_line(const char *line, uint16_t len) {
    /* 检测并跳过表头行 */
    if (!g_header_parsed) {
        if (len >= 12 && memcmp(line, "\\"Designator\\"", 12) == 0) {
            g_header_parsed = true;
            PrintDebug("[HOST] Header detected, skipping.\\r\\n");
            return false;
        }
        /* 首行非表头：当作无表头 CSV，直接开始解析数据 */
        g_header_parsed = true;
    }

    /* 提取 SMD (列12)，判断是否保留 */
    char smd[8] = {0};
    if (!csv_get_field(line, len, 12, smd, sizeof(smd))) {
        return false;
    }

    /* SMD 过滤：白名单模式 — 仅保留 "Yes" 和 "MARK"，其余一律丢弃 */
    bool is_mark = (strcmp(smd, "MARK") == 0);
    if (!is_mark && strcmp(smd, "Yes") != 0) {
        return false;
    }

    /* 分配存储位置 */
    Component_t *c;
    if (is_mark) {
        if (g_mark_count >= MAX_MARKS) {
            PrintDebug("[HOST] WARN: too many marks (>%d), ignoring\\r\\n", MAX_MARKS);
            return false;
        }
        c = &g_marks[g_mark_count];
    } else {
        if (g_comp_count >= MAX_COMPONENTS) {
            PrintDebug("[HOST] WARN: too many components (>%d), ignoring\\r\\n", MAX_COMPONENTS);
            return false;
        }
        c = &g_components[g_comp_count];
    }

    memset(c, 0, sizeof(*c));
    c->feeder_id = 1;

    char tmp[32];

    /* col[2] Footprint — 封装名称 */
    if (csv_get_field(line, len, 2, tmp, sizeof(tmp))) {
        uint16_t n = (uint16_t)strlen(tmp);
        if (n >= sizeof(c->footprint)) n = sizeof(c->footprint) - 1;
        memcpy(c->footprint, tmp, n);
        c->footprint[n] = '\\0';
    }

    /* col[3] Mid X — 含 "mm" 后缀 */
    if (csv_get_field(line, len, 3, tmp, sizeof(tmp))) {
        c->target_x = parse_mm(tmp);
    }

    /* col[4] Mid Y — 含 "mm" 后缀 */
    if (csv_get_field(line, len, 4, tmp, sizeof(tmp))) {
        c->target_y = parse_mm(tmp);
    }

    /* col[10] Layer — "T" 或 "B" */
    if (csv_get_field(line, len, 10, tmp, sizeof(tmp))) {
        c->layer = (tmp[0] == 'T' || tmp[0] == 'B') ? tmp[0] : 'T';
    } else {
        c->layer = 'T';
    }

    /* col[11] Rotation — 角度（度） */
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

'''

# Insert before download_done
content = content.replace(
    "static void download_done(void) {",
    new_parse_csv + "static void download_done(void) {"
)

# 7. Update download_done for marks
old_dd = '''static void download_done(void) {
    PrintDebug("[HOST] Download done. %u components.\\r\\n", g_comp_count);
    g_header_parsed = false;

    if (g_comp_count > 0) {
        g_comp_index = 0;
        g_mark_count_done = 0;
        memset(g_mark_offsets, 0, sizeof(g_mark_offsets));
        g_state = HOST_MARK_ALIGN;
        Vision_Start(VCMD_P2);
        Vision_Go();
        PrintDebug("[HOST] Starting Mark alignment (P2)...\\r\\n");
    } else {
        host_send("DOWNLOAD_READY");
        g_state = HOST_DEBUG;
    }
}'''

new_dd = '''static void download_done(void) {
    PrintDebug("[HOST] Download done. %u marks, %u components.\\r\\n",
               g_mark_count, g_comp_count);
    g_header_parsed = false;

    if (g_mark_count > 0) {
        /* 有 Mark 点：先执行 P2 建系 */
        g_comp_index = 0;
        g_mark_count_done = 0;
        memset(g_mark_offsets, 0, sizeof(g_mark_offsets));
        g_state = HOST_MARK_ALIGN;
        Vision_Start(VCMD_P2, 0);
        Vision_Go();
        PrintDebug("[HOST] Starting Mark alignment (P2, %u marks)...\\r\\n", g_mark_count);
    } else if (g_comp_count > 0) {
        /* 无 Mark 点但有元件：直接开始 P1 找元件 */
        g_comp_index = 0;
        move_xy_relative(FEEDER_AREA_X_STEPS - g_cur_x, FEEDER_AREA_Y_STEPS - g_cur_y,
                         PNP_SPEED, PNP_ACC, &g_cur_x, &g_cur_y);
        Vision_Start(VCMD_P1, 0);
        g_state = HOST_FIND_COMP;
        PrintDebug("[HOST] No marks, starting find component (P1)...\\r\\n");
    } else {
        /* 空文件 */
        host_send("DOWNLOAD_READY");
        g_state = HOST_DEBUG;
    }
}'''

content = content.replace(old_dd, new_dd)

print("User changes applied successfully")
print(f"Content now {len(content)} bytes")

with open(fpath, "w", encoding="utf-8") as f:
    f.write(content)

print("Written successfully")
