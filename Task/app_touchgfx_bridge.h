#ifndef __APP_TOUCHGFX_BRIDGE_H
#define __APP_TOUCHGFX_BRIDGE_H

/*
 * app_touchgfx_bridge.h — 主控端 ↔ TouchGFX 屏幕对接层
 *
 * 职责：
 *   1. 封装 System→GUI 通知（温度、进度、状态、速度、WiFi、日志）
 *   2. 实现 GUI→System 命令处理（电机移动/停止/回零、SMT启停、加热、复位）
 *
 * 约束：
 *   不直接访问 TouchGFX View/Presenter/Widget，仅通过 Data_Transfer 接口通信。
 *   温度/坐标/进度/速度 均使用整数单位，避免浮点进队列。
 */

#include <stdint.h>

/* ======================== 初始化 ======================== */
void Bridge_Init(void);

/* ======================== System → GUI 通知 API ======================== */

/* 温度通知（单位 0.1°C） */
void Bridge_NotifyTemp(int16_t temp_0_1c);

/* 贴片状态通知：1=贴片中, 0=空闲 */
void Bridge_NotifySMTStatus(uint8_t is_smt);

/* 贴片进度通知：current/total（高频可调用） */
void Bridge_NotifySMTProgress(uint8_t current, uint8_t total);

/* 下载/导入状态：1=下载中, 0=完成 */
void Bridge_NotifyDownloadStatus(uint8_t status);

/* 电机默认速度通知（低频，启动时一次） */
void Bridge_NotifyMotorSpeed(uint16_t speed);

/* 电机复位完成通知 */
void Bridge_NotifyMotorResetDone(void);

/* WiFi 连接状态：1=已连接, 0=断开 */
void Bridge_NotifyWifiStatus(uint8_t connected);

/* 短日志通知（code:param 格式，GUI 端显示） */
void Bridge_NotifyLog(uint8_t code, uint8_t param);

/* ======================== 加热台状态轮询（在 Host_Task 主循环中调用） ======================== */
/* 读取 heater_rx_queue 中的最新状态帧，更新 GUI 温度显示 */
void Bridge_ProcessHeaterStatus(void);

/* ======================== GUI → System 命令处理（由 Data_Transfer.c 回调） ======================== */
/* 以下函数由 TouchGFX 侧的 Data_Transfer.c 路由表调用，不应由主控端直接调用 */

void Bridge_MotorMove(int32_t x, int32_t y, int32_t r);
void Bridge_MotorStop(void);
void Bridge_MotorHome(void);
void Bridge_SMTStart(void);
void Bridge_SMTPause(void);
void Bridge_HeaterSet(uint16_t temp);
void Bridge_SystemReset(void);

/* ======================== GUI 触发的全局标志（Host_Task 主循环检测） ======================== */
extern volatile uint8_t g_gui_smt_start_req;   /* GUI 请求启动贴片 */
extern volatile uint8_t g_gui_smt_pause_req;   /* GUI 请求暂停贴片 */

#endif
