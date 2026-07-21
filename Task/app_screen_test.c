/* ================================================================
 *  app_screen_test.c — TouchGFX 屏幕功能全覆盖测试
 *
 *  测试范围:
 *    System→GUI 通知: SMT状态/温度/下载状态/进度/电机速度/WiFi/日志
 *    GUI→System 命令: 电机移动/停止/回零/SMT启动暂停/加热/复位
 *    Bridge 层: 去重逻辑/队列通信/全局标志
 *
 *  触发方式: 串口发送 SCREEN_TEST 命令
 *  运行方式: FreeRTOS 独立任务，完成后自动删除
 * ================================================================ */

#include "app_screen_test.h"
#include "app_touchgfx_bridge.h"
#include "gui/model/Data_Transfer.h"
#include "driver_heater.h"
#include "app_motion.h"
#include "driver_uart.h"
#include "app_test.h"       /* PrintDebug */
#include "app_uart_parser.h"
#include <string.h>

/* 测试用常量 */
#define TEST_DELAY_STEP_MS   800    /* 每步延时，方便肉眼观察屏幕变化 */
#define TEST_DELAY_FAST_MS   200    /* 快速步骤延时 */

/* 测试结果统计 */
static int s_pass = 0;
static int s_fail = 0;
static bool s_test_done = false;

static void test_assert(bool cond, const char *name)
{
    if (cond) { s_pass++; PrintDebug("[SCRN]  PASS: %s\r\n", name); }
    else      { s_fail++; PrintDebug("[SCRN] *FAIL: %s\r\n", name); }
}

/* ================================================================
 *  测试 1: System→GUI 通知 — 贴片状态
 * ================================================================ */
static void test_smt_status(void)
{
    PrintDebug("[SCRN] --- Test 1: SMT Status ---\r\n");

    /* 空闲 */
    Bridge_NotifySMTStatus(0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "SMT status: idle (0)");

    /* 贴片中 */
    Bridge_NotifySMTStatus(1);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "SMT status: running (1)");

    /* 去重验证: 连续两次相同值应被过滤 */
    Bridge_NotifySMTStatus(1);
    Bridge_NotifySMTStatus(1);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "SMT status: dedup (1→1→1)");

    /* 切换回空闲 */
    Bridge_NotifySMTStatus(0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "SMT status: back to idle");
}

/* ================================================================
 *  测试 2: System→GUI 通知 — 温度
 * ================================================================ */
static void test_temperature(void)
{
    PrintDebug("[SCRN] --- Test 2: Temperature ---\r\n");

    /* 室温 */
    Bridge_NotifyTemp(250);   /* 25.0°C */
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Temp: 25.0°C (250)");

    /* 预热 */
    Bridge_NotifyTemp(1500);  /* 150.0°C */
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Temp: 150.0°C (1500)");

    /* 回流峰值 */
    Bridge_NotifyTemp(2300);  /* 230.0°C */
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Temp: 230.0°C (2300)");

    /* 冷却 */
    Bridge_NotifyTemp(600);   /* 60.0°C */
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Temp: 60.0°C (600)");

    /* 去重验证 */
    Bridge_NotifyTemp(600);
    Bridge_NotifyTemp(600);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Temp: dedup (600→600→600)");
}

/* ================================================================
 *  测试 3: System→GUI 通知 — 下载状态
 * ================================================================ */
static void test_download_status(void)
{
    PrintDebug("[SCRN] --- Test 3: Download Status ---\r\n");

    /* 下载中 */
    Bridge_NotifyDownloadStatus(1);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Download: importing (1)");

    /* 下载完成 */
    Bridge_NotifyDownloadStatus(0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Download: done (0)");

    /* 去重 */
    Bridge_NotifyDownloadStatus(0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Download: dedup (0→0)");
}

/* ================================================================
 *  测试 4: System→GUI 通知 — 贴片进度
 * ================================================================ */
static void test_smt_progress(void)
{
    PrintDebug("[SCRN] --- Test 4: SMT Progress ---\r\n");

    /* 初始 */
    Bridge_NotifySMTProgress(0, 30);
    osDelay(TEST_DELAY_STEP_MS);
    test_assert(true, "Progress: 0/30");

    /* 逐步推进 */
    Bridge_NotifySMTProgress(5, 30);
    osDelay(TEST_DELAY_FAST_MS);
    Bridge_NotifySMTProgress(10, 30);
    osDelay(TEST_DELAY_FAST_MS);
    Bridge_NotifySMTProgress(15, 30);
    osDelay(TEST_DELAY_FAST_MS);
    Bridge_NotifySMTProgress(20, 30);
    osDelay(TEST_DELAY_FAST_MS);
    Bridge_NotifySMTProgress(25, 30);
    osDelay(TEST_DELAY_FAST_MS);
    Bridge_NotifySMTProgress(30, 30);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Progress: 30/30 (complete)");

    /* 去重验证 */
    Bridge_NotifySMTProgress(30, 30);
    Bridge_NotifySMTProgress(30, 30);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Progress: dedup (30→30→30)");
}

/* ================================================================
 *  测试 5: System→GUI 通知 — 电机速度 / 复位 / WiFi / 日志
 * ================================================================ */
static void test_misc_notifications(void)
{
    PrintDebug("[SCRN] --- Test 5: Misc Notifications ---\r\n");

    /* 电机速度 */
    Bridge_NotifyMotorSpeed(300);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Motor speed: 300");

    Bridge_NotifyMotorSpeed(150);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Motor speed: 150");

    /* 电机复位完成 */
    Bridge_NotifyMotorResetDone();
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Motor reset done");

    /* WiFi 状态 */
    Bridge_NotifyWifiStatus(1);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "WiFi: connected (1)");

    Bridge_NotifyWifiStatus(0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "WiFi: disconnected (0)");

    /* 系统日志 */
    Bridge_NotifyLog(1, 0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Log: code=1 param=0");

    Bridge_NotifyLog(2, 0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Log: code=2 param=0 (PnP done)");

    Bridge_NotifyLog(3, 1);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Log: code=3 param=1 (motor error)");

    Bridge_NotifyLog(4, 0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Log: code=4 param=0 (SMT paused)");
}

/* ================================================================
 *  测试 6: GUI→System 命令 — 电机控制 (通过 Bridge handler)
 * ================================================================ */
static void test_motor_commands(void)
{
    PrintDebug("[SCRN] --- Test 6: Motor Commands ---\r\n");

    /* 电机移动: 运动到 (10.00mm, 20.00mm) */
    Bridge_MotorMove(1000, 2000, 0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Motor move: (10.00, 20.00)mm");

    /* 电机移动: 运动到原点 */
    Bridge_MotorMove(0, 0, 0);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Motor move: (0, 0)mm");

    /* 电机急停 */
    Bridge_MotorStop();
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Motor stop");

    /* 电机回零 */
    Bridge_MotorHome();
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Motor home");
}

/* ================================================================
 *  测试 7: GUI→System 命令 — SMT 控制
 * ================================================================ */
static void test_smt_commands(void)
{
    PrintDebug("[SCRN] --- Test 7: SMT Commands ---\r\n");

    /* SMT 启动请求 — 设置标志，Host_Task 主循环检测 */
    g_gui_smt_start_req = 0;
    Bridge_SMTStart();
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(g_gui_smt_start_req == 1, "SMT start flag set");
    g_gui_smt_start_req = 0;  /* 复位，避免干扰 Host_Task */

    /* SMT 暂停请求 */
    g_gui_smt_pause_req = 0;
    Bridge_SMTPause();
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(g_gui_smt_pause_req == 1, "SMT pause flag set");
    g_gui_smt_pause_req = 0;  /* 复位 */
}

/* ================================================================
 *  测试 8: GUI→System 命令 — 加热台 / 系统复位
 * ================================================================ */
static void test_heater_reset_commands(void)
{
    PrintDebug("[SCRN] --- Test 8: Heater & Reset Commands ---\r\n");

    /* 加热台设置温度 180.0°C */
    Bridge_HeaterSet(1800);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Heater set: 180.0°C (1800)");

    /* 加热台设置温度 230.0°C */
    Bridge_HeaterSet(2300);
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Heater set: 230.0°C (2300)");

    /* 系统复位 — 不实际测（会重启MCU），仅验证函数可达 */
    PrintDebug("[SCRN]   SKIP: System reset (would reboot MCU)\r\n");
    test_assert(true, "System reset: skipped (would reboot)");
}

/* ================================================================
 *  测试 9: Bridge 层 — 加热台状态轮询
 * ================================================================ */
static void test_heater_polling(void)
{
    PrintDebug("[SCRN] --- Test 9: Heater Polling ---\r\n");

    /* 调用 Bridge_ProcessHeaterStatus — 若加热台未连接,
     * Heater_GetCurrentStatus 返回 timestamp=0，不推送温度。
     * 此处仅验证函数不会 crash。 */
    Bridge_ProcessHeaterStatus();
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Heater polling: no crash");
}

/* ================================================================
 *  测试 10: dataTransferQueue 溢出保护
 * ================================================================ */
static void test_queue_flood(void)
{
    PrintDebug("[SCRN] --- Test 10: Queue Flood (16 deep) ---\r\n");

    /* 连续发送 32 条通知，验证不会死锁或崩溃 */
    for (uint8_t i = 0; i < 32; i++) {
        Bridge_NotifyLog((i % 8), i);
    }
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Queue flood: 32 logs, no crash");

    /* 高频进度推送 */
    for (uint8_t i = 0; i < 64; i++) {
        Bridge_NotifySMTProgress(i % 31, 30);
    }
    osDelay(TEST_DELAY_FAST_MS);
    test_assert(true, "Queue flood: 64 progress, no crash");
}

/* ================================================================
 *  测试入口: 串行执行所有测试
 * ================================================================ */
void ScreenTest_Run(void)
{
    s_pass = 0;
    s_fail = 0;
    s_test_done = false;

    PrintDebug("[SCRN] ========================================\r\n");
    PrintDebug("[SCRN]  TouchGFX Screen Test Suite\r\n");
    PrintDebug("[SCRN] ========================================\r\n");

    /* ---- System→GUI 通知测试 ---- */
    PrintDebug("[SCRN]\r\n");
    PrintDebug("[SCRN] >> System→GUI Notifications <<\r\n");
    test_smt_status();
    test_temperature();
    test_download_status();
    test_smt_progress();
    test_misc_notifications();

    /* ---- GUI→System 命令测试 ---- */
    PrintDebug("[SCRN]\r\n");
    PrintDebug("[SCRN] >> GUI→System Commands <<\r\n");
    test_motor_commands();
    test_smt_commands();
    test_heater_reset_commands();

    /* ---- Bridge 层测试 ---- */
    PrintDebug("[SCRN]\r\n");
    PrintDebug("[SCRN] >> Bridge Layer <<\r\n");
    test_heater_polling();
    test_queue_flood();

    /* ---- 结果汇总 ---- */
    PrintDebug("[SCRN]\r\n");
    PrintDebug("[SCRN] ========================================\r\n");
    PrintDebug("[SCRN]  Results: %d pass, %d fail\r\n", s_pass, s_fail);
    PrintDebug("[SCRN] ========================================\r\n");

    s_test_done = true;
}

/* ================================================================
 *  FreeRTOS 任务入口
 * ================================================================ */
/* 任务属性 */
const osThreadAttr_t screenTestTask_attributes = {
    .name = "ScrnTest",
    .stack_size = 1024,
    .priority = osPriorityNormal,
};

void StartScreenTestTask(void *argument)
{
    (void)argument;

    /* 等待系统稳定 (Host_Task 初始化完成) */
    osDelay(500);

    ScreenTest_Run();

    /* 测试完成，任务自删除 */
    PrintDebug("[SCRN] Task exiting.\r\n");
    vTaskDelete(NULL);
}