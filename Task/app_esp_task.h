#ifndef __APP_ESP_TASK_H
#define __APP_ESP_TASK_H

/**
 * @file    app_esp_task.h
 * @brief   ESP32 通信任务 — 周期数据推送 + 控制命令路由 + 响应处理
 * @note    版本: v2
 *          依赖: driver_esp32.h, app_esp_protocol.h
 *          复用: now_SMT, total_SMT, Temp, if_now_SMT, if_DOWNLOAD_READY
 *                (Data_Transfer.c), Heater_GetCurrentStatus() (driver_heater.c)
 */

#include <stdint.h>
#include "cmsis_os2.h"

/* ================================================================
 *  ESP 控制命令类型 (放入 esp_cmd_queue 的消息)
 * ================================================================ */
typedef enum {
    ESP_CMD_WIFI_ON,          /* 打开 WiFi */
    ESP_CMD_WIFI_OFF,         /* 关闭 WiFi */
    ESP_CMD_QUERY_FAULT,      /* 查询故障码 */
    ESP_CMD_QUERY_WIFI,       /* 查询 WiFi 状态 */
} ESP_Cmd_t;

/* ================================================================
 *  全局状态标志位 (其他模块可 extern 引用)
 *  ⚠️ 后续模块如需判断 ESP/WiFi 状态，优先复用这些标志位
 * ================================================================ */
extern uint8_t  g_esp_wifi_enabled;    /* 0=WiFi关闭, 1=WiFi已开启 (本地记录)  */
extern uint8_t  g_esp_wifi_connected;  /* 0=未连接, 1=已连接 (ESP 回传)        */
extern uint8_t  g_esp_fault_code;      /* 当前故障码, 0x00=无故障              */
extern uint32_t g_esp_last_rx_tick;    /* 最后一次收到 ESP 有效响应的 tick      */

/* ================================================================
 *  队列句柄
 * ================================================================ */
extern osMessageQueueId_t esp_cmd_queue;  /* 其他任务 -> ESP_Task 控制命令 */

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief  ESP 通信任务入口
 * @note   在 app_freertos.c 中通过 osThreadNew 创建
 *         栈大小: 512 字节, 优先级: Normal
 */
void ESP_Task(void *argument);

/**
 * @brief  便捷接口: 其他任务向 ESP_Task 发送控制命令
 * @param  cmd  命令类型
 * @note   非阻塞，队列满时静默丢弃
 */
void ESP_SendCommand(ESP_Cmd_t cmd);

#endif
