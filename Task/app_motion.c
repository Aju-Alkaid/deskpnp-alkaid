#include "app_motion.h"
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

/* ---------- 电机地址定义 ---------- */
#define X1_ADDR   0x01
#define X2_ADDR   0x02
#define Y_ADDR    0x03

/* ---------- 舵机配置定义 ---------- */
#define SERVO_CH_Z   2        // 对应你在 Servo_Init 中配置的 channel 索引

#define ANGLE_UP      75.0f   // 吸嘴升起角度 / 安全高度 / P3偏移检测
#define ANGLE_DOWN  110.0f    // 吸嘴下降角度 / 吸取+贴装高度

// 吸嘴气泵由 DRV8803 12VO1 (PE11) 控制，见 driver_drv8803.h Pump_On/Off

// R 轴参数（需根据实测调整）
#define R_MICROSTEPS    256
#define R_STEPS_PER_REV (200 * R_MICROSTEPS)  // 51200 微步/圈
#define R_ACCEL_DELAY   50                    // 加减速/停止延时（ms）
#define R_RAMP_FRACTION 0.5f                  // 软启动斜坡起始速度比例

/* MKS SERVO42D 编码器参数 */
#define MKS_PULSES_PER_REV  16384.0f  /* 电机每圈脉冲数 */

/* 协议常量 */
#define FUNC_ABS_POS   0xF5       // 坐标绝对运动功能码
#define STATUS_SYNC_RECV  0x05    // 同步模式：已接收指令，等待同步执行
#define STATUS_RUN_COMPLETE 0x02  // 运行完成
#define STATUS_START      0x01    // 普通模式：运行开始 (未使用同步时可能收到)

#define ACK_TIMEOUT_MS   2000      // 单轴到位超时 (ms)

/* ================================================================
 *  座标核心 — 线程安全的机器座标系单例
 * ================================================================ */
static MachineCoord_t g_coord;
static osMutexId_t    g_coord_mutex;

void Coord_Init(void) {
    if (g_coord_mutex != NULL) return;  /* 幂等，允许多处调用 */
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
 *        acc=0 时立即停止，否则减速停止
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
 * @brief 急停三轴（同步模式，0xF7 立即停止 + 同步触发）
 * @note  Motor_Init 已开启同步（motorSyncEnable(1)），
 *        axis_stop 经 motorSyncTrigger 同步触发执行。
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
 * @brief  XY 相对移动（阻塞式，按时长估算到位）
 *
 * 发送 F4 相对位置指令到指定轴 → 广播同步触发 → 按时长估算等待到位 →
 * 检测 g_axes_error 堵转标志。CAN_Process_Task 异步消费 CAN 帧并更新
 * g_axes_done_bits / g_axes_error 全局标志。
 *
 * @param  dx     X 轴相对位移 (步数，X1+X2 双电机同步)
 * @param  dy     Y 轴相对位移 (步数，单电机)
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

    /* 重置到位标志，供 CAN_Process_Task 更新 */
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

    /* 按时长估算到位 = max(|dx|,|dy|) / (speed × PULSES_PER_REV / 60000) + 安全余量 */
    {
        float max_steps = fmaxf(fabsf((float)dx), fabsf((float)dy));
        float steps_per_ms = (float)speed * MKS_PULSES_PER_REV / 60000.0f;
        uint32_t move_ms = (uint32_t)(max_steps / steps_per_ms) + 80;
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
 * @brief 阻塞等待指定电机进入"运行完成"状态 (0x02)      已被事件组代替，暂无用
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
//                // 其他状态 (0x05 同步接收, 0x01 开始) 忽略，继续等待
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

// 阻塞式等待舵机稳定 (简单延时，MG995 大约 0.2s/60°)
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
    if (ret == -2) g_motor_error = true;   /* 限位/堵转，不可恢复 */
    return ret;
}

/* ---------- 组合的吸取 / 放置流程 ---------- */

bool pick_component(void) {
    z_pick();
    nozzle_on();
    servo_delay_ms(50);    // 真空建立
    if (!vacuum_ok()) {
        nozzle_off();
        z_safe();
        return false;       // 吸取失败
    }
    z_safe();
    return true;
}

/* 真空检测 — 当前无硬件，始终返回 true。接入 GPIO/ADC 后覆盖此函数 */
__weak bool vacuum_ok(void) {
    return true;
}

void place_component(void) {
    z_place();
    nozzle_off();
    servo_delay_ms(100);   // 释放
    z_safe();
}

/**
 * @brief 将角度转换为微步数
 * @param angle 角度 (0.0 ~ 360.0)
 * @return 微步数
 */
static int32_t angle_to_usteps(float angle) {
    return (int32_t)(angle / 360.0f * R_STEPS_PER_REV);
}

/**
 * @brief R 轴旋转到指定角度 (开环，基于时间)
 * @param angle  目标角度 (绝对角度，0~360)
 * @param speed_rpm 转速
 */
void r_axis_set_zero(void) {
    Coord_UpdateR(0.0f);
}

int r_axis_rotate(float angle, float speed_rpm) {
    MachineCoord_t c0 = Coord_Get();
    float delta = angle - c0.r;
    if (delta < -180.0f) delta += 360.0f;
    else if (delta > 180.0f) delta -= 360.0f;   // 选择最短路径

    if (fabsf(delta) <= R_CORRECTION_THRESHOLD_DEG) return -1;  // 最小矫正阈值，无动作

    uint8_t  dir = (delta >= 0) ? 0 : 1;
    int32_t  usteps = angle_to_usteps(fabsf(delta));
    float    full_spd = speed_rpm * R_STEPS_PER_REV / 60.0f; /* µsteps/s */
    float    ramp_spd = full_spd * R_RAMP_FRACTION;
    uint32_t ramp_usteps = (uint32_t)(ramp_spd * R_ACCEL_DELAY / 1000.0f);

    /* 使能 TMC2209 驱动 */
    TMC_SetEnable(true);
    vTaskDelay(pdMS_TO_TICKS(TMC_ENABLE_DELAY_MS));
    PrintDebug("[R] delta=%.2f deg usteps=%ld dir=%d\r\n", (double)delta, (long)usteps, (int)dir);

    if (usteps <= ramp_usteps * 2) {
        /* 小角度：降速保证足够运行时间，避免一步跳全速丢步 */
        int32_t low = (int32_t)(usteps * 1000.0f / (R_ACCEL_DELAY * 2));
        TMC_SetSpeed(dir ? -low : low);
        vTaskDelay(pdMS_TO_TICKS(R_ACCEL_DELAY * 2));
    } else {
        /* 正常角度：半速斜坡 → 全速恒速 */
        TMC_SetSpeed(dir ? -(int32_t)ramp_spd : (int32_t)ramp_spd);
        vTaskDelay(pdMS_TO_TICKS(R_ACCEL_DELAY));

        uint32_t remain = usteps - ramp_usteps;
        uint32_t run_ms = (uint32_t)(remain * 1000.0f / full_spd);
        TMC_SetSpeed(dir ? -(int32_t)full_spd : (int32_t)full_spd);
        vTaskDelay(pdMS_TO_TICKS(run_ms));
    }

    /* 停止 */
    TMC_SetSpeed(0);
    vTaskDelay(pdMS_TO_TICKS(R_ACCEL_DELAY));

    /* 关闭 TMC2209 驱动 */
    TMC_SetEnable(false);

    Coord_UpdateR(angle);
    return 0;
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
    Servo_Init(&htim5);       // 舵机初始化，参数按你的实际定时器填写
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
                    PrintDebug("Move to (%ld, %ld) done.\r\n", cmd.target_x, cmd.target_y);
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
                // 回零：向坐标 0 移动，速度稍慢
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
            r_axis_rotate((float)cmd.target_r / 100.0f, (float)cmd.speed);
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
 * @note 从motor_event_queue 取数据并释放信号量。
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
        }
    }
}