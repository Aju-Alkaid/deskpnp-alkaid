#ifndef __APP_UART_PARSER_H
#define __APP_UART_PARSER_H

#include <stdint.h>
#include <stdbool.h>

/* 行缓冲区最大长度 */
#define LINE_BUF_MAX    512

/* 上位机命令类型 */
typedef enum {
    HCMD_NONE = 0,
    /* 调试单步移动 */
    HCMD_MOVE_UP,       HCMD_MOVE_DOWN,
    HCMD_MOVE_LEFT,     HCMD_MOVE_RIGHT,
    /* 调试连续移动 */
    HCMD_MOVE_UP_START,    HCMD_MOVE_DOWN_START,
    HCMD_MOVE_LEFT_START,  HCMD_MOVE_RIGHT_START,
    HCMD_MOVE_STOP,
    /* 坐标系 */
    HCMD_SET_ORIGIN,
    HCMD_SET_SERVO,     // 舵机角度设定
    HCMD_SET_R_AXIS,    // R轴旋转角度
    HCMD_PUMP_ON,       // 开启气泵
    HCMD_PUMP_OFF,      // 关闭气泵
    HCMD_HEAT_ON,       // 开启加热台
    HCMD_HEAT_OFF,      // 关闭加热台
    HCMD_HEATER_QUERY,  // 查询加热台状态
    HCMD_HOLD_TEMP,     // 恒温保持模式
    HCMD_MOVE_TO,       // 运动至指定坐标
    /* 退出调试 */
    HCMD_EXIT_DEBUG,
    /* 文件下载 - 原始行 */
    HCMD_RAW_LINE,

    /* 标定命令 */
    HCMD_SET_SCATTER_AREA,    // 记录散料区原点
    HCMD_SET_SCATTER_SIZE,    // 散料区边长 <mm>
    HCMD_SET_HEATER_PLATFORM_MIN,    // 记录加热台左下角
    HCMD_SET_HEATER_PLATFORM_MAX,    // 记录加热台右上角
    HCMD_SET_BOTTOM_CAM,      // 记录下相机位置
    HCMD_SET_Z_SAFE,          // 记录安全高度
    HCMD_SET_Z_PICK,          // 记录吸取高度
    HCMD_SET_Z_PLACE,         // 记录贴装高度
    HCMD_SET_R_ZERO,          // R 轴当前位置 = 0deg
    HCMD_R_CALIB,             // R 轴编码器自动校准
    HCMD_MSCNT_TEST,          /* MSCNT 原始值采样诊断 */
   HCMD_SET_CAM_OFFSET,     // 上摄像头à吸嘴偏置标定 (两步)
    HCMD_SAVE_CALIB,          // 保存标定值到 Flash
    HCMD_RESTORE_CALIB,       // 恢复标定值（调试用）
    HCMD_RESUME,              // 从暂停/WAIT_REFILL/ERROR 恢复
    HCMD_ABORT,               // 中止当前流程 -> HOST_IDLE
    HCMD_AUTO_HEAT,           // AUTO_HEAT ON/OFF — 自动回流焊开关

    HCMD_HOME,                // 回到原点
    HCMD_VALVE_ON,            // 开启电磁阀
    HCMD_VALVE_OFF,           // 关闭电磁阀
    HCMD_LIGHT,               // 切换下补光灯
    HCMD_CALIB_ENC,           // P2编码器比例标定

    /* 未知命令 */
    HCMD_UNKNOWN
} HostCmd_t;

/* 解析后的上位机命令 */
typedef struct {
    HostCmd_t cmd;
    float param;            /* 步长(mm)或速度(mm/s) */
    float param2;           // Y 坐标(mm) (MOVE_TO 使用)
    char  raw[LINE_BUF_MAX]; /* 原始行内容(文件下载时使用) */
    uint16_t raw_len;
} HostParsed_t;

/* 行解析器状态机 */
typedef struct {
    char    buf[LINE_BUF_MAX];
    uint16_t idx;
    bool    complete;       /* 已收到完整一行 */
} LineParser_t;

/* ---- API ---- */
void LineParser_Init(LineParser_t *p);

/* 喂入一个字节，返回 true 表示收到完整一行(结果写入 out) */
bool LineParser_Feed(LineParser_t *p, uint8_t byte, HostParsed_t *out);

/* 构建单片机→上位机消息(返回长度，不含结尾 \0) */
uint16_t LineParser_BuildMsg(const char *msg, char *buf, uint16_t buf_size);

#endif
