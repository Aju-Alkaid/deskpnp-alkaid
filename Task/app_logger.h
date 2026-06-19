#ifndef __APP_LOGGER_H
#define __APP_LOGGER_H

#include <stdint.h>

/* W25Q64 日志区 — 倒数第二个 4KB 扇区 (sector 2046) */
#define LOG_SECTOR_ADDR   0x7FE000
#define LOG_SECTOR_SIZE   4096
#define LOG_ENTRY_SIZE    16
#define LOG_MAX_ENTRIES   255          /* (4096-8)/16 = 255 */
#define LOG_HEADER_SIZE   8            /* magic(4) + write_offset(4) */
#define LOG_HEADER_MAGIC  0x474F4C00U  /* "LOG\0" little-endian */

/* ---- 事件码 ---- */
typedef enum {
    LOG_PNP_START    = 0x01,  /* PnP 开始: comp_cnt_H, comp_cnt_L, mark_cnt, 0... */
    LOG_PNP_DONE     = 0x02,  /* PnP 完成: comp_cnt_H, comp_cnt_L, 0... */
    LOG_PNP_ERROR    = 0x03,  /* PnP 错误: error_type, comp_idx_H, comp_idx_L, 0... */
    LOG_MOTOR_ERROR  = 0x04,  /* 电机异常: err_detail(1=TIMEOUT,2=LIMIT), 0... */
    LOG_HEATER_START = 0x05,  /* 加热台启动: 全 0 */
    LOG_HEATER_DONE  = 0x06,  /* 加热台完成: final_state, 0... */
    LOG_ABORT        = 0x07,  /* 用户中止: 全 0 */
} LogEvent_t;

/* ---- 条目 (16 字节，压缩对齐) ---- */
typedef struct __attribute__((packed)) {
    uint8_t  event;
    uint8_t  reserved;
    uint16_t seq;           /* 单调递增，溢出回绕 */
    uint32_t tick;          /* osKernelGetTickCount() */
    uint8_t  data[8];       /* 事件附带数据 */
} LogEntry_t;

/* ---- API ---- */
void Log_Init(void);
void Log_Write(LogEvent_t event, const uint8_t *data);

#endif
