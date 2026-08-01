#ifndef __APP_ESP_TASK_H
#define __APP_ESP_TASK_H

/**
 * @file    app_esp_task.h
 * @brief   ESP32 通信任务 — 周期数据推送 + 控制命令路由 + 响应处理 + 场景B IRQ
 * @note    版本: v2 (新增 IRQ 场景B, CMD 0x40/0x50/0x60)
 *          依赖: driver_esp32.h, app_esp_protocol.h
 */

#include <stdint.h>
#include "cmsis_os2.h"

/* ================================================================
 *  ESP 控制命令类型 (放入 esp_cmd_queue 的消息)
 * ================================================================ */
typedef enum {
    ESP_CMD_WIFI_ON,          /* 打开 WiFi                               */
    ESP_CMD_WIFI_OFF,         /* 关闭 WiFi                               */
    ESP_CMD_QUERY_FAULT,      /* 查询故障码                              */
    ESP_CMD_QUERY_WIFI,       /* 查询 WiFi 状态                          */
    ESP_CMD_QUERY_ALL,        /* 查询全部状态 (v2 新增)                  */

    /* ★ v2 新增: 网页下发的流程/加热台命令 (ESP→STM32, 经 IRQ 场景B 接收) */
    ESP_CMD_PROC_START,       /* 网页 "开始" 按钮                        */
    ESP_CMD_PROC_PAUSE,       /* 网页 "暂停" 按钮                        */
    ESP_CMD_PROC_RESUME,      /* 网页 "继续" 按钮                        */
    ESP_CMD_PROC_STOP,        /* 网页 "结束" 按钮                        */
    ESP_CMD_PROC_ESTOP,       /* 网页 "急停" 按钮                        */
    ESP_CMD_HEAT_START,       /* 网页 "开启加热" 按钮                    */
    ESP_CMD_HEAT_STOP,        /* 网页 "暂停加热" 按钮                    */
} ESP_Cmd_t;

/* ================================================================
 *  全局状态标志位 (其他模块可 extern 引用)
 * ================================================================ */
extern uint8_t  g_esp_wifi_enabled;    /* 0=WiFi关闭, 1=WiFi已开启        */
extern uint8_t  g_esp_wifi_connected;  /* 0=未连接, 1=已连接 (ESP 回传)  */
extern uint8_t  g_esp_fault_code;      /* 当前故障码, 0x00=无故障        */
extern uint32_t g_esp_last_rx_tick;    /* 最后收到 ESP 有效响应的 tick    */

/* ★ v2 新增: 网页下发命令标志 (ESP_Task 处理后转为队列消息通知 Host_Task) */
extern volatile uint8_t g_esp_web_cmd_pending; /* 有待处理的网页命令        */
extern volatile ESP_Cmd_t g_esp_web_cmd;       /* 当前网页命令类型          */

/* ================================================================
 *  队列句柄
 * ================================================================ */
extern osMessageQueueId_t esp_cmd_queue;  /* 其他任务 -> ESP_Task 控制命令 */

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief  ESP 通信任务入口
 * @note   栈大小: 512 字节, 优先级: Normal
 */
void ESP_Task(void *argument);

/**
 * @brief  便捷接口: 其他任务向 ESP_Task 发送控制命令
 * @param  cmd  命令类型
 * @note   非阻塞，队列满时静默丢弃
 */
void ESP_SendCommand(ESP_Cmd_t cmd);

/**
 * @brief  发送日志到 ESP32 (CMD_LOG_DATA 0x50) ★ v2 新增
 * @param  text  UTF-8 日志文本 (自动截断至 123 字节)
 * @note   内部构建 128 字节帧 + SPI 全双工收发 + 处理 ESP 响应
 *         线程安全: 仅通过 ESP_Task 的队列机制调用
 */
void ESP_SendLog(const char *text);

#endif