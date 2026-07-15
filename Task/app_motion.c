#include "app_motion.h"

//#define DEBUG_MOTION  // cancel comment to enable motion debug log
#include "driver_motor.h"
#include "driver_can.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stdbool.h>
#include "driver_servo.h"
#include "driver_tmc2209.h"
#include <math.h>   // 解决 fabsf 未声明
#include "app_test.h"
#include "driver_drv8803.h"
#include "app_config.h"
#include "driver_uart.h"
#include "timestamp.h"

extern TIM_HandleTypeDef htim5;


//信号量

//事件组
osEventFlagsId_t evtAxesDone = NULL;   // 用于三轴到位同步
volatile bool g_motor_error = false;
volatile MotorError_t g_motor_error_detail = MOTOR_OK;
volatile bool s_cmd_interrupted = false;
volatile uint32_t g_axes_done_bits = 0;
volatile bool g_axes_error = false;

/* ---- 31H encoder position stash (P2 stop-and-read) ---- */
volatile int32_t g_enc_pos[4] = {0};  /* ID 1-3 */
volatile bool    g_enc_ready[4] = {false};

/* ---------- 电机地址定义 ---------- */
#define X1_ADDR   0x01
#define X2_ADDR   0x02
#define Y_ADDR    0x03

/* ---------- 舵机配置定义 ---------- */
#define SERVO_CH_Z   2        // 对应你在 Servo_Init 中配置的 channel 索引

#define ANGLE_UP      75.0f   // 吸嘴升起角度 / 安全高度 / P3偏移检测
#define ANGLE_DOWN  110.0f    // 吸嘴下降角度 / 吸取+贴装高度

// 吸嘴气泵由 DRV8803 12VO1 (PE11) 控制,见 driver_drv8803.h Pump_On/Off


/* MKS SERVO42D 编码器参数 */
#define MKS_PULSES_PER_REV  16384.0f  /* 电机每圈脉冲数 */

/* 协议常量 */
#define FUNC_ABS_POS   0xF5       // 坐标绝对运动功能码
#define STATUS_SYNC_RECV  0x05    // 同步模式：已接收指令,等待同步执行
#define STATUS_RUN_COMPLETE 0x02  // 运行完成
#define STATUS_START      0x01    // 普通模式：运行开始 (未使用同步时可能收到)

#define ACK_TIMEOUT_MS   2000      // 单轴到位超时 (ms)

static uint32_t g_move_pad_ms = 3000;  /* move_xy_relative 安全余量,点动时临时改为 80 */
/* ================================================================
 *  座标核心 - 线程安全的机器座标系单例
 * ================================================================ */
static MachineCoord_t g_coord;
static osMutexId_t    g_coord_mutex;

void Coord_Init(void) {
    if (g_coord_mutex != NULL) return;  /* 幂等,允许多处调用 */
    g_coord_mutex = osMutexNew(NULL);
    memset(&g_coord, 0, sizeof(g_coord));
    g_coord.z = 75.0f;                  /* 默认安全角度 */
}

MachineCoord_t Coord_Get(void) {
    MachineCoord_t c;
    if (g_coord_mutex) {
        osMutexAcquire(g_coord_mutex, osWaitForever);
        c = g_coord;
        osMutexRelease(g_coord_mutex);
    } else {
        c = g_coord;                    /* 初始化前的安全回退 */
    }
    return c;
}

void Coord_SetHome(void) {
    if (g_coord_mutex) osMutexAcquire(g_coord_mutex, osWaitForever);
    g_coord.x = 0;
    g_coord.y = 0;
    g_coord.r = 0.0f;
    g_coord.homed = true;
    g_coord.valid = true;
    if (g_coord_mutex) osMutexRelease(g_coord_mutex);
}

void Coord_UpdateXY(int32_t x, int32_t y) {
    if (g_coord_mutex) osMutexAcquire(g_coord_mutex, osWaitForever);
    g_coord.x = x;
    g_coord.y = y;
    g_coord.valid = true;               /* 到位 = 座标可信 */
    if (g_coord_mutex) osMutexRelease(g_coord_mutex);
}

void Coord_UpdateR(float angle) {
    if (g_coord_mutex) osMutexAcquire(g_coord_mutex, osWaitForever);
    g_coord.r = angle;
    if (g_coord_mutex) osMutexRelease(g_coord_mutex);
}

void Coord_UpdateZ(float angle) {
    if (g_coord_mutex) osMutexAcquire(g_coord_mutex, osWaitForever);
    g_coord.z = angle;
    if (g_coord_mutex) osMutexRelease(g_coord_mutex);
}

void Coord_Invalidate(void) {
    if (g_coord_mutex) osMutexAcquire(g_coord_mutex, osWaitForever);
    g_coord.valid = false;              /* 保留上次坐标值供诊断 */
    if (g_coord_mutex) osMutexRelease(g_coord_mutex);
}

/* ---------- 内部辅助函数 ---------- */

/**
 * @brief 发送单轴坐标绝对运动指令 (不等待)
 */
static void send_axis_abs(int32_t addr, int32_t abs_coord, uint16_t speed, uint8_t acc)
{
    // 坐标范围限制在 int24_t (-8388607 ~ 8388607)
    if (abs_coord >  8388607) abs_coord =  8388607;
    if (abs_coord < -8388607) abs_coord = -8388607;

    positionMode3Run(addr, speed, acc, abs_coord);  // 直接调用你的底层驱动
}

/**
 * @brief 向指定电机发送急停（立即减速停止）
 *        acc=0 时立即停止,否则减速停止
 */
static void send_axis_stop(int32_t addr)
{
    uint8_t tx[8] = {0};
    tx[0] = 0xF7;  // 立即停止（不受同步模式影响）
    CAN_Transmit_Data(&hfdcan1, addr, tx, 1);
}

/* ---- XY 相对移动与急停（从 app_test.c 迁移）---- */

/**
 * @brief 发送急停指令到指定电机轴
 * @param addr 电机 CAN ID (0x01/0x02/0x03)
 */
void axis_stop(int32_t addr)
{
    uint8_t tx[8] = {0};
    tx[0] = 0xF7;  // 立即停止（不受同步模式影响）
    CAN_Transmit_Data(&hfdcan1, addr, tx, 1);
}
/**
 * @brief 急停三轴（同步模式,0xF7 立即停止 + 同步触发）
 * @note  Motor_Init 已开启同步（motorSyncEnable(1)）,
 *        axis_stop 经 motorSyncTrigger 同步触发执行.
 */
void disable_sync_stop(void)
{
    axis_stop(X1_ADDR);
    axis_stop(X2_ADDR);
    axis_stop(Y_ADDR);
    motorSyncTrigger(0);
    osDelay(5);
}

/**
 * @brief  XY 相对移动（阻塞式,按时长估算到位）
 *
 * 发送 F4 相对位置指令到指定轴 → 广播同步触发 → 按时长估算等待到位 →
 * 检测 g_axes_error 堵转标志.CAN_Process_Task 异步消费 CAN 帧并更新
 * g_axes_done_bits / g_axes_error 全局标志.
 *
 * @param  dx     X 轴相对位移 (步数,X1+X2 双电机同步)
 * @param  dy     Y 轴相对位移 (步数,单电机)
 * @param  speed  速度 (RPM)
 * @param  acc    加速度
 * @retval  0  到位
 * @retval -2  电机异常 (堵转/限位)
 */
int move_xy_relative(int32_t dx, int32_t dy, uint16_t speed, uint8_t acc)
{
    MachineCoord_t c0 = Coord_Get();
    int32_t target_x = c0.x + dx;
    int32_t target_y = c0.y + dy;

    if (target_x >  8388607) target_x =  8388607;
    if (target_x < -8388607) target_x = -8388607;
    if (target_y >  8388607) target_y =  8388607;
    if (target_y < -8388607) target_y = -8388607;

    if (dx == 0 && dy == 0) {
        return 0;
    }

    /* 重置到位标志,供 CAN_Process_Task 更新 */
    g_axes_done_bits = 0;
    g_axes_error = false;

    if (dx != 0) {
        positionMode2Run(X1_ADDR, speed, acc, dx);
        osDelay(2);
        positionMode2Run(X2_ADDR, speed, acc, dx);
        osDelay(2);
    }
    if (dy != 0) {
        positionMode2Run(Y_ADDR,  speed, acc, dy);
        osDelay(2);
    }

    /* 广播同步触发: X1/X2/Y 同时执行 */
    motorSyncTrigger(0);

    /* 按时长估算到位 = max(|dx|,|dy|) / (speed ˇ PULSES_PER_REV / 60000) + 安全余量 */
    {
        float max_steps = fmaxf(fabsf((float)dx), fabsf((float)dy));
        float steps_per_ms = (float)speed * MKS_PULSES_PER_REV / 60000.0f;
        uint32_t move_ms = (uint32_t)(max_steps / steps_per_ms) + g_move_pad_ms;
        if (move_ms < 50)  move_ms = 50;
        if (move_ms > 5000) move_ms = 5000;
        osDelay(move_ms);
    }

    /* 检查运动期间是否发生堵转/限位 */
    if (g_axes_error) {
        axis_stop(X1_ADDR);
        axis_stop(X2_ADDR);
        axis_stop(Y_ADDR);
        g_motor_error_detail = MOTOR_ERR_LIMIT;
        Coord_Invalidate();
        return -2;
    }

    Coord_UpdateXY(target_x, target_y);
    return 0;}

/**
 * @brief 阻塞等待指定电机进入"运行完成"状态 (0x02)      已被事件组代替,暂无用
 * @param addr 电机 CAN ID
 * @param timeout_ms 超时 (ms)
 * @retval 0 成功, -1 超时
 */
//static int wait_axis_ready(int32_t addr, uint32_t timeout_ms)
//{
//    uint32_t start = osKernelGetTickCount();
//    CAN_Rx_Packet_t pkt;

//    while ((osKernelGetTickCount() - start) < timeout_ms) {
//        if (osMessageQueueGet(motor_event_queue, &pkt, NULL, 10) == osOK) {
//            // 只关心当前电机的坐标绝对运动响应
//            if (pkt.ID == addr && pkt.FuncCode == FUNC_ABS_POS) {
//                if (pkt.Status == STATUS_RUN_COMPLETE) {
//                    return 0;   // 到位
//                }
//                // 其他状态 (0x05 同步接收, 0x01 开始) 忽略,继续等待
//            }
//        }
//    }
//    return -1;  // 超时
//}

/**
 * @brief 执行一次完整的 X/Y 同步移动
 * @param x_abs  X 轴绝对坐标 (两个 X 电机目标相同)
 * @param y_abs  Y 轴绝对坐标
 * @param speed  速度 (RPM)
 * @param acc    加速度
 * @retval 0 成功, -1 失败
 */
static int move_to(int32_t x_abs, int32_t y_abs, uint16_t speed, uint8_t acc)
{
    g_axes_done_bits = 0;
    g_axes_error = false;
    send_axis_abs(X1_ADDR, x_abs, speed, acc);
    send_axis_abs(X2_ADDR, x_abs, speed, acc);
    send_axis_abs(Y_ADDR,  y_abs, speed, acc);
    motorSyncTrigger(0);

    uint32_t waited = 0;
    while (waited < ACK_TIMEOUT_MS) {
        osDelay(10); waited += 10;
        if (g_axes_error)                        return -2;
        if ((g_axes_done_bits & EVENT_ALL_AXES) == EVENT_ALL_AXES) return 0;
    }
    return -1;
}

void nozzle_on(void) {
    DRV8803_SetOutput(&Port_12VO1, true);
}
void nozzle_off(void) {
    DRV8803_SetOutput(&Port_12VO1, false);
}

/* ---------- 封装 Z 轴基本动作 ---------- */

// 阻塞式等待舵机稳定 (简单延时,MG995 大约 0.2s/60°)
static void servo_delay_ms(uint32_t ms) {
    osDelay(ms);   // FreeRTOS 下的毫秒延时
}

void z_down(void) {
    Servo_SetAngle(SERVO_CH_Z, ANGLE_DOWN);
    servo_delay_ms(300);   // 等待到位
    Coord_UpdateZ(ANGLE_DOWN);
}

void z_up(void) {
    Servo_SetAngle(SERVO_CH_Z, ANGLE_UP);
    servo_delay_ms(300);
    Coord_UpdateZ(ANGLE_UP);
}

/* ---------- Z 轴三高度 (使用 g_calib 标定值) ---------- */

void z_safe(void) {
    Servo_SetAngle(SERVO_CH_Z, g_calib.z_safe_angle);
    servo_delay_ms(300);
    Coord_UpdateZ(g_calib.z_safe_angle);
}

void z_pick(void) {
    Servo_SetAngle(SERVO_CH_Z, g_calib.z_pick_angle);
    servo_delay_ms(300);
    Coord_UpdateZ(g_calib.z_pick_angle);
}

void z_place(void) {
    Servo_SetAngle(SERVO_CH_Z, g_calib.z_place_angle);
    servo_delay_ms(300);
    Coord_UpdateZ(g_calib.z_place_angle);
}

/* ---------- 安全 XY 运动 (移动前自动抬 Z 到安全高度) ---------- */

int safe_move_to(int32_t target_x, int32_t target_y, uint16_t speed, uint8_t acc) {
    z_safe();
    osDelay(100);
    MachineCoord_t c0 = Coord_Get();
    int32_t dx = target_x - c0.x;
    int32_t dy = target_y - c0.y;
    int ret = move_xy_relative(dx, dy, speed, acc);
    if (ret == -2) g_motor_error = true;   /* 限位/堵转,不可恢复 */
    return ret;
}

void move_set_pad_ms(uint32_t pad_ms) { g_move_pad_ms = pad_ms; }

/* ---------- 组合的吸取 / 放置流程 ---------- */

bool pick_component(void) {
    z_pick();
    servo_delay_ms(300);   // Z轴完全稳定后再开泵
    nozzle_on();
    servo_delay_ms(500);   // 真空建立
    if (!vacuum_ok()) {
        nozzle_off();
        z_safe();
        return false;       // 吸取失败
    }
    z_safe();
    return true;
}

/* 真空检测 - 当前无硬件,始终返回 true.接入 GPIO/ADC 后覆盖此函数 */
__weak bool vacuum_ok(void) {
    return true;
}

void place_component(void) {
    z_place();
    nozzle_off();
    Valve_On();
    servo_delay_ms(800);   // 电磁阀吹气辅助元件脱离
    Valve_Off();
    z_safe();
}

/* ========== R轴 时间积分开环 (TMC2209 VACTUAL 速度模式, 无编码器) ========== */

/* ---- 内部子状态 ---- */
typedef enum {
    R_SUB_IDLE,
    R_SUB_EN_DELAY,     /* 等待 TMC 使能稳定 (50ms) */
    R_SUB_INIT,          /* RAMPMODE 方向 + 初始速度 (5ms) */
    R_SUB_RUNNING,       /* PID 闭环调速 (每 8ms 一轮) */
    R_SUB_STOP           /* 停转 + 去使能 (20ms) */
} R_Sub_t;

/* ---- 模块级状态 ---- */
static R_State_t g_r_state = R_IDLE;
static R_Sub_t   g_r_sub   = R_SUB_IDLE;
static uint32_t  g_r_deadline;       /* 当前子阶段截止时间 (ms tick) */
static int32_t   g_r_target;         /* 目标微步数 */
static int32_t   g_r_position;       /* 时间积分估算位置 (usteps) */
static int32_t   g_r_spd_cmd;        /* 当前速度指令 (Hz, 有符号) */
static int32_t   g_r_max_spd;        /* 最大速度限制 */
static int32_t   g_r_stable;         /* 到位连续稳定计数 */
static uint32_t  g_r_t0;             /* 旋转开始时间 (超时用) */
static float     g_r_cmd_angle;      /* 成功后 Coord_UpdateR 用 */

void r_axis_set_zero(void) {
    PrintDebug("[R] Zero set (time-integration ref)\r\n");
}

float r_axis_calibrate(void) {
    return 0.0f;
}

void r_axis_start(float angle, float speed_rpm) {
    if (g_r_state == R_BUSY) {
        PrintDebug("[R] BUSY: start ignored during rotation\r\n");
        return;
    }

    g_r_target    = R_DEG_TO_USTEPS(angle);
    g_r_cmd_angle = angle;

    if (g_r_target == 0) {
        Coord_UpdateR(angle);
        g_r_state = R_DONE;
        g_r_sub   = R_SUB_IDLE;
        return;
    }

    g_r_max_spd = (int32_t)(speed_rpm * R_STEPS_PER_REV / 60.0f);
    if (g_r_max_spd > R_MAX_SPEED) g_r_max_spd = R_MAX_SPEED;
    if (g_r_max_spd < R_MIN_SPEED) g_r_max_spd = R_MIN_SPEED;

    g_r_position = 0;
    g_r_spd_cmd  = (g_r_target >= 0) ? R_MIN_SPEED : -R_MIN_SPEED;
    g_r_stable   = 0;
    g_r_t0       = HAL_GetTick();

    TMC_SetEnable(true);
    g_r_deadline = HAL_GetTick() + TMC_ENABLE_DELAY_MS;
    g_r_sub      = R_SUB_EN_DELAY;
    g_r_state    = R_BUSY;
}

void r_axis_poll(void) {
    if (g_r_state != R_BUSY) return;

    uint32_t now = HAL_GetTick();

    switch (g_r_sub) {

    case R_SUB_EN_DELAY:
        if ((int32_t)(now - g_r_deadline) < 0) return;
        /* 使能稳定，设置 RAMPMODE 方向 + 初始 VACTUAL */
        TMC_SetSpeed(g_r_spd_cmd);
        g_r_deadline = now + 5;
        g_r_sub = R_SUB_INIT;
        break;

    case R_SUB_INIT:
        if ((int32_t)(now - g_r_deadline) < 0) return;
        /* 进入 PID 调速循环 */
        g_r_deadline = now + R_POLL_INTERVAL_MS;
        g_r_sub = R_SUB_RUNNING;
        break;

    case R_SUB_RUNNING:
        if ((int32_t)(now - g_r_deadline) < 0) return;
        g_r_deadline = now + R_POLL_INTERVAL_MS;

        {
            /* ---- 积分: 当前速度 * 轮询间隔 ---- */
            g_r_position += (g_r_spd_cmd * (int32_t)R_POLL_INTERVAL_MS) / 1000;

            /* ---- DRV_STATUS.stst 硬件卡死判定 ---- */
            uint32_t drv;
            if (TMC_GetDRVStatus(&drv) == TMC_ERR_NONE) {
                if ((drv & (1u << 31)) && g_r_spd_cmd != 0) {
                    TMC_SetSpeedDirect(0);
                    TMC_SetEnable(false);
                    g_r_state = R_STUCK;
                    g_r_sub   = R_SUB_IDLE;
                    PrintDebug("[R] STUCK: stst=1 while VACTUAL!=0\r\n");
                    return;
                }
            }

            /* ---- SG_RESULT 堵转检测 (R_SG_THRESHOLD=0 时禁能) ---- */
            if (R_SG_THRESHOLD > 0) {
                uint16_t sg;
                if (TMC_GetSGResult(&sg) == TMC_ERR_NONE) {
                    int32_t abs_spd = (g_r_spd_cmd >= 0) ? g_r_spd_cmd : -g_r_spd_cmd;
                    if (abs_spd >= R_SG_MIN_SPEED && sg < R_SG_THRESHOLD) {
                        TMC_SetSpeedDirect(0);
                        TMC_SetEnable(false);
                        g_r_state = R_STALL;
                        g_r_sub   = R_SUB_IDLE;
                        PrintDebug("[R] STALL: SG_RESULT=%u < %u\r\n",
                                   (unsigned)sg, (unsigned)R_SG_THRESHOLD);
                        return;
                    }
                }
            }

            /* ---- 到位判断 ---- */
            int32_t err = g_r_target - g_r_position;
            int32_t abs_err = (err >= 0) ? err : -err;
            if (abs_err < R_POS_TOLERANCE) {
                g_r_stable++;
                if (g_r_stable >= R_STABLE_COUNT) {
                    TMC_SetSpeedDirect(0);
                    g_r_deadline = now + 20;
                    g_r_sub = R_SUB_STOP;
                    return;
                }
            } else {
                g_r_stable = 0;
            }

            /* ---- 速度计算: 比例 + 基础巡航 + 加速度限制 ---- */
            int32_t spd = (int32_t)((float)abs_err * R_PID_KP) + R_MIN_SPEED;
            if (spd > g_r_max_spd) spd = g_r_max_spd;
            if (spd < R_MIN_SPEED) spd = R_MIN_SPEED;

            int32_t prev_abs = (g_r_spd_cmd >= 0) ? g_r_spd_cmd : -g_r_spd_cmd;
            if (spd > prev_abs + 1000) spd = prev_abs + 1000;

            g_r_spd_cmd = (err >= 0) ? spd : -spd;
            TMC_SetSpeedDirect(g_r_spd_cmd);

            /* ---- 超时 ---- */
            if (now - g_r_t0 > R_TIMEOUT_MS) {
                TMC_SetSpeedDirect(0);
                TMC_SetEnable(false);
                g_r_state = R_TIMEOUT;
                g_r_sub   = R_SUB_IDLE;
                PrintDebug("[R] TIMEOUT: pos=%ld target=%ld\r\n",
                           (long)g_r_position, (long)g_r_target);
                return;
            }
        }
        break;

    case R_SUB_STOP:
        if ((int32_t)(now - g_r_deadline) < 0) return;
        /* 停转完成，去使能电机 */
        TMC_SetSpeedDirect(0);
        TMC_SetEnable(false);
        Coord_UpdateR(g_r_cmd_angle);
        g_r_state = R_DONE;
        g_r_sub   = R_SUB_IDLE;
        PrintDebug("[R] Done: pos=%ld steps (target=%ld)\r\n",
                   (long)g_r_position, (long)g_r_target);
        break;

    default:
        break;
    }
}

R_State_t r_axis_state(void) {
    return g_r_state;
}/*  ================================================================
 *  P2 连续扫描运动控制 - X1+X2 同步速度模式 (0xF6)
 *  仅 X1(0x01)+X2(0x02) 同步,Y 轴侧移独立.
 *  X2 CAN 状态不可用,全程使用时间估算代替到位信号.
 * ================================================================ */

/**
 * @brief P2 扫描启动 - X1+X2 同步速度模式 (0xF6)
 * @param dir   方向: 0=CCW, 1=CW (@see P2_SCAN_DIR_UP/DOWN)
 * @param speed 速度 (RPM)
 * @param acc   加速度
 */
void p2_scan_start(uint8_t dir, uint16_t speed, uint8_t acc)
{
    g_axes_done_bits = 0;
    g_axes_error = false;
    speedModeRun(X1_ADDR, dir, speed, acc);
    osDelay(2);
    speedModeRun(X2_ADDR, dir, speed, acc);
    osDelay(2);
    motorSyncTrigger(0);
}

/**
 * @brief P2 扫描停止 - X1+X2 立即停止 (0xF7) + 同步触发
 */
void p2_scan_stop(void)
{
    axis_stop(X1_ADDR);
    axis_stop(X2_ADDR);
    motorSyncTrigger(0);
    osDelay(5);
}

/**
 * @brief P2 扫描位置估算 - 基于速度和时间的 X1/X2 轴位移
 * @param start_x    列起点 X 坐标 (步数)
 * @param sign       方向符号 (+1 = UP, -1 = DOWN)
 * @param speed      速度 (RPM)
 * @param elapsed_ticks 列启动后经过的 tick 数 (假设 1tick=1ms)
 * @return 估算的当前 X 坐标 (步数)
 */
int32_t p2_scan_estimate_x(int32_t start_x, int32_t sign, uint16_t speed, uint32_t elapsed_ticks)
{
    float steps_per_ms = (float)speed * 16384.0f / 60000.0f;
    int32_t dx = (int32_t)((float)sign * steps_per_ms * (float)elapsed_ticks);
    return start_x + dx;
}

/**
 * @brief P2 扫描侧移 - Y 电机相对位移 (阻塞式,时长估算到位)
 * @param dy_steps Y 轴相对位移 (步数)
 * @param speed    速度 (RPM)
 * @param acc      加速度
 */
void p2_scan_step_y(int32_t dy_steps, uint16_t speed, uint8_t acc)
{
    if (dy_steps == 0) return;
    g_axes_done_bits = 0;
    g_axes_error = false;
    positionMode2Run(Y_ADDR, speed, acc, dy_steps);
    motorSyncTrigger(0);
    {
        float steps_per_ms = (float)speed * 16384.0f / 60000.0f;
        uint32_t move_ms = (uint32_t)(fabsf((float)dy_steps) / steps_per_ms) + 200;
        if (move_ms < 50)  move_ms = 50;
        if (move_ms > 5000) move_ms = 5000;
        osDelay(move_ms);
    }
    if (!g_axes_error) {
        Coord_UpdateXY(Coord_Get().x, Coord_Get().y + dy_steps);
    }
}

/**
 * @brief P2 扫描停止并读取 X1/X2 电机真实编码器位置 (31H)
 * @return X1+X2 平均位置 (步数), -1 表示读取失败
 */
int32_t p2_stop_and_read_pos(int32_t enc_start_x1, int32_t enc_start_x2, int32_t coord_start_x)
{
    /* Step 1: stp收到时刻立即读编码器 (停止前, 电机仍在速度模式) */
    g_enc_ready[X1_ADDR] = false;
    readRealTimeLocation(X1_ADDR);
    { uint32_t t0 = osKernelGetTickCount();
      while (!g_enc_ready[X1_ADDR] && (osKernelGetTickCount() - t0) < 100) { osDelay(2); } }
    int32_t enc_detect_x1 = g_enc_ready[X1_ADDR] ? g_enc_pos[X1_ADDR] : enc_start_x1;

    g_enc_ready[X2_ADDR] = false;
    readRealTimeLocation(X2_ADDR);
    { uint32_t t0 = osKernelGetTickCount();
      while (!g_enc_ready[X2_ADDR] && (osKernelGetTickCount() - t0) < 100) { osDelay(2); } }
    int32_t enc_detect_x2 = g_enc_ready[X2_ADDR] ? g_enc_pos[X2_ADDR] : enc_start_x2;

    /* Step 2: 停止电机并等待减速完成 */
    axis_stop(X1_ADDR);
    axis_stop(X2_ADDR);
    motorSyncTrigger(0);
    osDelay(200);

    /* Step 3: 读取停止后编码器 */
    g_enc_ready[X1_ADDR] = false;
    readRealTimeLocation(X1_ADDR);
    { uint32_t t0 = osKernelGetTickCount();
      while (!g_enc_ready[X1_ADDR] && (osKernelGetTickCount() - t0) < 100) { osDelay(2); } }
    if (!g_enc_ready[X1_ADDR]) { return -1; }
    int32_t enc_stop_x1 = g_enc_pos[X1_ADDR];

    g_enc_ready[X2_ADDR] = false;
    readRealTimeLocation(X2_ADDR);
    { uint32_t t0 = osKernelGetTickCount();
      while (!g_enc_ready[X2_ADDR] && (osKernelGetTickCount() - t0) < 100) { osDelay(2); } }
    if (!g_enc_ready[X2_ADDR]) { return -1; }
    int32_t enc_stop_x2 = g_enc_pos[X2_ADDR];

    /* Step 4: 计算 overshoot 并反向补偿 (位置模式, 精确回退) */
    int32_t overshoot_x1_enc = enc_stop_x1 - enc_detect_x1;
    int32_t overshoot_x2_enc = enc_stop_x2 - enc_detect_x2;
    int32_t overshoot_avg_enc = (overshoot_x1_enc + overshoot_x2_enc) / 2;
    int32_t back_steps = -P2_ENC2STEP(overshoot_avg_enc);

    /* 回退量上限 3mm (1500步), 防止编码器异常导致大幅回退 */
    if (back_steps > 1500)  back_steps = 1500;
    if (back_steps < -1500) back_steps = -1500;

    if (back_steps != 0) {
        g_axes_done_bits = 0;
        g_axes_error = false;
        motorSyncEnable(1);
        osDelay(5);
        positionMode2Run(X1_ADDR, 100, 50, back_steps);
        positionMode2Run(X2_ADDR, 100, 50, back_steps);
        motorSyncTrigger(0);
        osDelay(500);  /* 小位移补偿, 500ms足够 */
    }

    /* Step 5: 修正坐标 = 列起点 + stp时刻位移 (不含overshoot) */
    int32_t dx1_enc = enc_detect_x1 - enc_start_x1;
    int32_t dx2_enc = enc_detect_x2 - enc_start_x2;
    int32_t avg_dx_enc = (dx1_enc + dx2_enc) / 2;
    int32_t dx_steps = P2_ENC2STEP(avg_dx_enc);

    int32_t real_x = coord_start_x + dx_steps;

    PrintDebug("[P2] 31H enc_detect=(%ld,%ld) enc_stop=(%ld,%ld) overshoot_enc=%ld back_step=%ld real_x=%ld\r\n",
               (long)enc_detect_x1, (long)enc_detect_x2, (long)enc_stop_x1, (long)enc_stop_x2,
               (long)overshoot_avg_enc, (long)back_steps, (long)real_x);
    return real_x;
}
/* ---------- 任务入口 ---------- */
void MotionTask_Func(void *argument)
{
    /* ----- 1. 初始化资源和电机 ----- */
    //if (motor_event_queue == NULL) {
    //    motor_event_queue = osMessageQueueNew(32, sizeof(CAN_Rx_Packet_t), NULL);
    //}

    // 电机硬件初始化 (包括设置模式、使能、同步标志、零点等)
    Motor_Init();   // 你的 Motor_Init 已经包含了同步使能(0x4A)
    Servo_Init(&htim5);       // 舵机初始化,参数按你的实际定时器填写
    TMC_Init();               // TMC2209 初始化
    // 可选：将开机当前位置设为工作零点
    // motorSetZero(0x01); ...

    MotionCmd_t cmd;

    /* ----- 2. 主循环 ----- */
    while (1) {
        if (osMessageQueueGet(motion_cmd_queue, &cmd, NULL, osWaitForever) == osOK) {
            switch (cmd.cmd_type) {

            case MOTION_CMD_MOVE_TO:

                g_axes_done_bits = 0;
                g_axes_error = false;
                positionMode3Run(X1_ADDR, cmd.speed, cmd.acc, cmd.target_x);
                positionMode3Run(X2_ADDR, cmd.speed, cmd.acc, cmd.target_x);
                positionMode3Run(Y_ADDR, cmd.speed, cmd.acc, cmd.target_y);
                motorSyncTrigger(0);

                uint32_t waited = 0;
                uint32_t flags = 0;
                while (waited < ACK_TIMEOUT_MS) {
                    osDelay(10); waited += 10;
                    if (g_axes_error) { flags = EVENT_ANY_ERROR; break; }
                    if ((g_axes_done_bits & EVENT_ALL_AXES) == EVENT_ALL_AXES) { flags = EVENT_ALL_AXES; break; }
                }

                if (flags & EVENT_ANY_ERROR) {
                    // 急停处理
                    send_axis_stop(X1_ADDR);
                    send_axis_stop(X2_ADDR);
                    send_axis_stop(Y_ADDR);
                    PrintDebug("Emergency stop! \r\n");
                    Coord_Invalidate();
                } else if ((flags & EVENT_ALL_AXES) == EVENT_ALL_AXES) {
#ifdef DEBUG_MOTION
                    PrintDebug("Move to (%ld, %ld) done.\r\n", cmd.target_x, cmd.target_y);
#endif
                    Coord_UpdateXY(cmd.target_x, cmd.target_y);
                }else {
                    // 超时处理
                    send_axis_stop(X1_ADDR);
                    send_axis_stop(X2_ADDR);
                    send_axis_stop(Y_ADDR);
                    PrintDebug("Move timeout! \r\n");
                    Coord_Invalidate();
                }
                break;

            case MOTION_CMD_HOME:
                // 回零：向坐标 0 移动,速度稍慢
                if (move_to(0, 0, 100, 50) == 0) {
                    Coord_SetHome();
                }
                break;

            case MOTION_CMD_STOP:
                // 所有轴急停 (立即停止)
                send_axis_stop(X1_ADDR);
                send_axis_stop(X2_ADDR);
                send_axis_stop(Y_ADDR);
                Coord_Invalidate();
                break;

            case MOTION_CMD_DISABLE:
                motorEnable(X1_ADDR, 0);
                motorEnable(X2_ADDR, 0);
                motorEnable(Y_ADDR, 0);
                break;
            case MOTION_CMD_Z_DOWN:   z_down(); break;

            case MOTION_CMD_Z_UP:     z_up();   break;

            case MOTION_CMD_PICK:     (void)pick_component(); break;

            case MOTION_CMD_PLACE:    place_component(); break;

            case MOTION_CMD_WAIT:
                osDelay(cmd.param2);
                break;

            case MOTION_CMD_R_ROTATE:
            r_axis_start((float)cmd.target_r / 100.0f, (float)cmd.speed);  /* 非阻塞启动, 由 Host_Task r_axis_poll 推进 */
            break;   // cmd.target_r 可约定为 0.01° 单位
						
            default:
                break;
            }
        }
    }
}
//信号量初始化
//事件组初始化
void Event_Init(void) {
    evtAxesDone = osEventFlagsNew(NULL);
}

/**
 * @brief can处理任务函数
 * @note 从motor_event_queue 取数据并释放信号量.
 */

void CAN_Process_Task(void *argument) {
    CAN_Rx_Packet_t pkt;
    while (1) {
        // 阻塞等待队列数据
        if (osMessageQueueGet(motor_event_queue, &pkt, NULL, osWaitForever) == osOK) {
            if ((pkt.FuncCode == 0xF4 || pkt.FuncCode == 0xF5) && pkt.Status == 0x02) {
                if (pkt.ID == 1)      g_axes_done_bits |= EVENT_X1_DONE;
                else if (pkt.ID == 2) g_axes_done_bits |= EVENT_X2_DONE;
                else if (pkt.ID == 3) g_axes_done_bits |= EVENT_Y_DONE;
            }
            else if ((pkt.FuncCode == 0xF4 || pkt.FuncCode == 0xF5) && pkt.Status == 0x03) {
                g_axes_error = true;
            }
            else if (pkt.FuncCode == 0x31 && pkt.ID >= 1 && pkt.ID <= 3 && pkt.DataLength >= 7) {
                /* 31H encoder: 48-bit signed big-endian Data[1..6], Data[7]=checksum */
                int64_t hi = ((int64_t)pkt.Data[1] << 24) | ((int64_t)pkt.Data[2] << 16) |
                            ((int64_t)pkt.Data[3] << 8)  |  (int64_t)pkt.Data[4];
                int64_t lo = ((int64_t)pkt.Data[5] << 8)  |  (int64_t)pkt.Data[6];
                int64_t enc48 = (hi << 16) | lo;
                enc48 = (enc48 << 16) >> 16;  /* sign-extend from 48 bits */
                g_enc_pos[pkt.ID] = (int32_t)enc48;
                g_enc_ready[pkt.ID] = true;
            }
        }
    }
}