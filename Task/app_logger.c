#include "app_logger.h"
#include "driver_spiflash_w25q64.h"
#include "cmsis_os2.h"
#include <stdbool.h>
#include <string.h>

static uint16_t g_seq = 0;
static uint32_t g_write_offset = 8;
static bool     g_inited = false;

static uint32_t read_header_u32(uint32_t off) {
    uint8_t b[4];
    if (W25Q64_Read(LOG_SECTOR_ADDR + off, b, 4) < 0) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static int write_header_u32(uint32_t off, uint32_t val) {
    uint8_t b[4];
    b[0] = (uint8_t)(val);
    b[1] = (uint8_t)(val >> 8);
    b[2] = (uint8_t)(val >> 16);
    b[3] = (uint8_t)(val >> 24);
    return W25Q64_Write(LOG_SECTOR_ADDR + off, b, 4);
}

static uint16_t recover_seq(void) {
    uint32_t wo = read_header_u32(4);
    if (wo <= LOG_HEADER_SIZE || wo > LOG_SECTOR_SIZE) return 0;
    uint32_t last_off = wo - LOG_ENTRY_SIZE;
    LogEntry_t e;
    if (W25Q64_Read(LOG_SECTOR_ADDR + last_off, (uint8_t*)&e, sizeof(e)) < 0)
        return 0;
    return e.seq + 1;
}

void Log_Init(void) {
    uint32_t magic = read_header_u32(0);
    if (magic == LOG_HEADER_MAGIC) {
        g_write_offset = read_header_u32(4);
        if (g_write_offset < LOG_HEADER_SIZE
            || g_write_offset > LOG_SECTOR_SIZE - LOG_ENTRY_SIZE) {
            g_write_offset = LOG_HEADER_SIZE;
        }
        g_seq = recover_seq();
    } else {
        W25Q64_Erase(LOG_SECTOR_ADDR, LOG_SECTOR_SIZE);
        write_header_u32(0, LOG_HEADER_MAGIC);
        write_header_u32(4, LOG_HEADER_SIZE);
        g_write_offset = LOG_HEADER_SIZE;
        g_seq = 1;
    }
    g_inited = true;
}

void Log_Write(LogEvent_t event, const uint8_t *data) {
    if (!g_inited) return;
    if (g_write_offset + LOG_ENTRY_SIZE > LOG_SECTOR_SIZE) {
        W25Q64_Erase(LOG_SECTOR_ADDR, LOG_SECTOR_SIZE);
        write_header_u32(0, LOG_HEADER_MAGIC);
        write_header_u32(4, LOG_HEADER_SIZE);
        g_write_offset = LOG_HEADER_SIZE;
    }
    LogEntry_t e;
    e.event    = (uint8_t)event;
    e.reserved = 0;
    e.seq      = g_seq;
    e.tick     = osKernelGetTickCount();
    if (data) memcpy(e.data, data, 8);
    else      memset(e.data, 0, 8);
    W25Q64_Write(LOG_SECTOR_ADDR + g_write_offset, (uint8_t*)&e, sizeof(e));
    g_write_offset += LOG_ENTRY_SIZE;
    g_seq++;
    write_header_u32(4, g_write_offset);
}
