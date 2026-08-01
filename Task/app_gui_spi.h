#ifndef __APP_GUI_SPI_H
#define __APP_GUI_SPI_H

#include <stdint.h>
#include "cmsis_os2.h"

/*
 * app_gui_spi.h — G4 主控 ↔ G0B1 GUI 从机 SPI 通信模块
 *
 * 物理层：G0B1 SPI1 <-> G4 SPI2，G4 为主机，G0B1 为从机，字符串命令协议，\n 结尾。
 * 协议规范见《01-GUI通信接口清单.md》。
 *
 * 方向定义：
 *   G0B1 → G4：用户 GUI 操作触发的命令（通过 SPI 读回）
 *   G4 → G0B1：主控主动推送的数据/状态（通过 SPI 发送）
 *
 * G0B1→G4 接收机制：
 *   从机拉低 GUI_INT 引脚触发 EXTI 中断 → ISR 设置标志
 *   → GUI_SPI_Task 轮询标志 → SPI 读事务获取命令行
 *   → 喂入 LineParser → 完整行放入 gui_cmd_queue 供 Host_Task 处理
 *
 * 约束：
 *   单条命令总长 ≤ 128 字节（SPI 接收缓冲区限制）
 *   LOG/IMPORT_DATA 内容最长 63 字符
 *   G4 SPI2 速率 10.625 Mbps（G0B1 最高 32 Mbps，留有余量）
 *   从机未使用的接收字节必须填充 0x00，主控在首个 0x00 停止解析
 */

/* ======================== 初始化 ======================== */

/* 初始化 SPI2、CS/INT GPIO、消息队列，在 FreeRTOS 启动前调用 */
void GUI_SPI_Init(void);

/* ======================== G4 → G0B1 数据推送 API ======================== */

/* 发送格式化命令字符串（自动追加 \n） */
void GUI_SPI_Send(const char *fmt, ...);

/* 温度通知（heat 和 pcb 为浮点摄氏温度） */
void GUI_SPI_NotifyTemp(float heat, float pcb);

/* 贴装进度通知 */
void GUI_SPI_NotifySMTStatus(uint8_t is_smt);

/* 贴装进度通知 */
void GUI_SPI_NotifySMTProgress(uint16_t current, uint16_t total);

/* 日志消息（最大 63 字符，超出截断） */
void GUI_SPI_NotifyLog(const char *msg);

/* WiFi 状态通知（state: "CONNECTED"/"CONNECTING"/"FAILED"/"DISCONNECTED"） */
void GUI_SPI_NotifyWifiStatus(const char *state);

/* 导入数据内容（单条，最大 63 字符） */
void GUI_SPI_NotifyImportData(const char *content);

/* 导入元件总数（用于 Home 页进度条） */
void GUI_SPI_NotifyImportTotal(uint16_t total);

/* ======================== G0B1 → G4 命令接收 API ======================== */

/* 轮询接收：非阻塞，返回 1 表示收到完整行（存入 buf），否则 0 */
int GUI_SPI_RecvPoll(char *buf, uint16_t bufsize);

/* SPI 接收任务入口（FreeRTOS 任务） */
void GUI_SPI_Task(void *argument);

/* EXTI 中断回调：G0B1 INT 引脚触发时由 HAL_GPIO_EXTI_Callback 调用 */
void GUI_SPI_IntCallback(uint16_t GPIO_Pin);

/* ======================== 配置宏（适配 CubeMX 引脚分配） ======================== */

/*
 * G4 SPI2 引脚：PB13(SCK)/PB14(MISO)/PB15(MOSI)（G0B1 使用其 SPI1）
 * 请在 CubeMX 中配置 SPI2；本模块使用阻塞收发，不使能 SPI NVIC。
 * 若改用其他引脚组，CubeMX 会自动生成正确的 hspi2 初始化。
 */

/* CS 引脚：PD10（原 LCD_CS，LCD 移除后释放） */
#define GUI_SPI_CS_PORT      GPIOD
#define GUI_SPI_CS_PIN       GPIO_PIN_10

/* INT 引脚：PD8（原 LCD_RST，LCD 移除后释放），G0B1 拉低表示有命令待发 */
#define GUI_SPI_INT_PORT     GPIOD
#define GUI_SPI_INT_PIN      GPIO_PIN_8

/* SPI 接收缓冲区大小（必须 ≥ 最大命令长度 128） */
#define GUI_SPI_RX_BUF_SIZE  128

/* 预留协议版本号：后续用于 G4/G0B1 握手与功能开关 */
#define GUI_PROTO_VERSION  1

/* ======================== 共享状态变量（原 TouchGFX Data_Transfer 全局） ======================== */

/* 以下变量由 G4 侧维护，供 ESP 任务等模块读取 */
extern uint8_t  if_now_SMT;         /* 1=贴片中, 0=空闲 */
extern uint8_t  total_SMT;          /* 元件总数 */
extern uint8_t  now_SMT;            /* 已完成数 */
extern uint8_t  if_DOWNLOAD_READY;  /* 1=导入完成 */

/* GUI 触发的全局标志（Host_Task 主循环检测） */
extern volatile uint8_t g_gui_smt_start_req;
extern volatile uint8_t g_gui_smt_pause_req;

/* GUI 命令队列（app_freertos.c 创建，Host_Task 消费） */
extern osMessageQueueId_t gui_cmd_queue;

/* SPI2 收发互斥锁（app_freertos.c 创建，Host/ESP/GUI 任务共用） */
extern osMutexId_t gui_spi_mutex;

#endif /* __APP_GUI_SPI_H */
