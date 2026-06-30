#include "app_touchgfx_bridge.h"
#include <stdbool.h>
#include "gui/model/Data_Transfer.h"
#include "driver_heater.h"
#include "app_motion.h"
#include "app_config.h"
#include "driver_motor.h"
#include <string.h>

/* ======================== 内部状态 ======================== */

/* 温度去重：跟踪上次发送值 + 有效标志（避免哨兵值 -1 与 -0.1°C 碰撞） */
static int16_t s_last_temp = 0;
static bool    s_temp_valid = false;

/* 贴片状态去重 */
static uint8_t s_last_smt_status = 0xFF;

/* 下载状态去重 */
static uint8_t s_last_download_status = 0xFF;

/* 进度去重：避免高频重复冲刷 dataTransferQueue（仅 16 深度） */
static uint8_t s_last_progress_cur   = 0xFF;
static uint8_t s_last_progress_total = 0xFF;

/* ======================== GUI 触发标志 ======================== */
volatile uint8_t g_gui_smt_start_req = 0;
volatile uint8_t g_gui_smt_pause_req = 0;

/* ======================== 初始化 ======================== */
void Bridge_Init(void)
{
    /* 重置所有去重状态（语义：支持运行中重初始化，如看门狗恢复） */
    s_temp_valid = false;
    s_last_temp = 0;
    s_last_smt_status = 0xFF;
    s_last_download_status = 0xFF;
    s_last_progress_cur   = 0xFF;
    s_last_progress_total = 0xFF;
    g_gui_smt_start_req = 0;
    g_gui_smt_pause_req = 0;
}

/* ======================== 温度通知 ======================== */
void Bridge_NotifyTemp(int16_t temp_0_1c)
{
    /* 去重：温度未变化不重复发送（使用有效标志避免哨兵值碰撞） */
    if (s_temp_valid && temp_0_1c == s_last_temp) return;
    s_last_temp = temp_0_1c;
    s_temp_valid = true;

    /* 转换为 uint16_t（加热台协议温度为有符号，GUI 接口为无符号） */
    DT_NotifyTemp((uint16_t)temp_0_1c);
}

/* ======================== 贴片状态通知 ======================== */
void Bridge_NotifySMTStatus(uint8_t is_smt)
{
    if (is_smt == s_last_smt_status) return;
    s_last_smt_status = is_smt;
    DT_NotifySMTStatus(is_smt);
}

/* ======================== 贴片进度通知 ======================== */
void Bridge_NotifySMTProgress(uint8_t current, uint8_t total)
{
    /* 去重：进度值未变化不重复发送，降低队列冲刷风险 */
    if (current == s_last_progress_cur && total == s_last_progress_total) return;
    s_last_progress_cur   = current;
    s_last_progress_total = total;
    DT_NotifySMTProgress(current, total);
}

/* ======================== 下载状态通知 ======================== */
void Bridge_NotifyDownloadStatus(uint8_t status)
{
    if (status == s_last_download_status) return;
    s_last_download_status = status;
    DT_NotifyDownloadStatus(status);
}

/* ======================== 电机速度通知 ======================== */
void Bridge_NotifyMotorSpeed(uint16_t speed)
{
    DT_NotifyMotorSpeed(speed);
}

/* ======================== 电机复位完成通知 ======================== */
void Bridge_NotifyMotorResetDone(void)
{
    DT_NotifyMotorResetDone();
}

/* ======================== WiFi 状态通知 ======================== */
void Bridge_NotifyWifiStatus(uint8_t connected)
{
    DT_NotifyWifiStatus(connected);
}

/* ======================== 短日志通知 ======================== */
void Bridge_NotifyLog(uint8_t code, uint8_t param)
{
    DT_NotifyLogText(code, param);
}

/* ======================== 加热台状态轮询 ======================== */
void Bridge_ProcessHeaterStatus(void)
{
    HeaterStatus_t hs = Heater_GetCurrentStatus();
    /* 仅在有温度数据时更新（timestamp > 0 表示收到过有效状态帧） */
    if (hs.timestamp > 0) {
        Bridge_NotifyTemp(hs.cur_temp);
    }
}

/* ================================================================
 *  GUI → System 命令处理（由 TouchGFX 侧 Data_Transfer.c 路由回调）
 * ================================================================ */

/* ---- 电机移动到目标坐标 ---- */
/* 坐标单位：mm × 100，由 GUI Presenter 转换后传入 */
void Bridge_MotorMove(int32_t x, int32_t y, int32_t r)
{
    /* 转换为步数 */
    int32_t steps_x = (int32_t)((float)x / 100.0f * STEPS_PER_MM);
    int32_t steps_y = (int32_t)((float)y / 100.0f * STEPS_PER_MM);

    /* 通过运动命令队列发送给 Host_Task 处理 */
    MotionCmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = MOTION_CMD_MOVE_TO;
    cmd.target_x = steps_x;
    cmd.target_y = steps_y;
    cmd.target_r = r;
    cmd.speed    = 300;   /* GUI 默认移动速度 RPM (与 PNP_SPEED 一致) */
    cmd.acc      = 25;    /* GUI 默认加速度 (与 PNP_ACC 一致) */

    if (motion_cmd_queue != NULL) {
        osMessageQueuePut(motion_cmd_queue, &cmd, 0, 0);
    }
}

/* ---- 电机急停 ---- */
void Bridge_MotorStop(void)
{
    /* 通过运动命令队列发送急停 */
    MotionCmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = MOTION_CMD_STOP;

    if (motion_cmd_queue != NULL) {
        osMessageQueuePut(motion_cmd_queue, &cmd, 0, 0);
    }
}

/* ---- 电机回零 ---- */
void Bridge_MotorHome(void)
{
    MotionCmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = MOTION_CMD_HOME;

    if (motion_cmd_queue != NULL) {
        osMessageQueuePut(motion_cmd_queue, &cmd, 0, 0);
    }
}

/* ---- 启动贴片流程 ---- */
void Bridge_SMTStart(void)
{
    g_gui_smt_start_req = 1;
}

/* ---- 暂停贴片流程 ---- */
void Bridge_SMTPause(void)
{
    g_gui_smt_pause_req = 1;
}

/* ---- 设置加热台温度（手动 PID 模式） ---- */
/* temp: 目标温度，单位 0.1°C。SET_TEMP 即进入手动 PID 控温，无需 START。 */
void Bridge_HeaterSet(uint16_t temp)
{
    Heater_SetTemperature((int16_t)temp);
}

/* ---- 系统软复位 ---- */
void Bridge_SystemReset(void)
{
    /* 各外设由各自任务管理，这里直接触发系统复位 */
    NVIC_SystemReset();
}
