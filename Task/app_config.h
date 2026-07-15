#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#include <stdint.h>

/* ---- 共享运动控制常量 ---- */
#define STEPS_PER_MM     512.0f
#define JOG_MAX_STEPS    8388607
#define X1_ADDR          0x01
#define X2_ADDR          0x02
#define Y_ADDR           0x03
/* R 轴旋转速度 (RPM) — 矫正和贴装共用 */
#define R_SPEED_RPM                 60.0f
#define R_CORRECTION_THRESHOLD_DEG  0.1f       /* R 轴视觉矫正死区 (deg) */

/* ---- R 轴 MSCNT 闭环 (TMC2209 VACTUAL 速度模式) ---- */
#define R_STEPS_PER_REV          51200       /* 200全步 × 256微步 = 51200 usteps/rev */
#define R_DEG_TO_USTEPS(d)       ((int32_t)((d) * R_STEPS_PER_REV / 360.0f))
#define R_POLL_INTERVAL_MS       8           /* PID 控制周期 (ms): 8ms保证MSCNT采样<512步 */
#define R_PID_KP                 4.0f        /* 比例系数 (usteps→Hz) */
#define R_MIN_SPEED              200         /* 最小 VACTUAL 频率 (Hz) */
#define R_MAX_SPEED              20000       /* 最大 VACTUAL 频率 = 23 RPM, 16ms内≤320步<512 */
#define R_POS_TOLERANCE          5           /* 到位容差 (usteps), 5步 = 0.035° */
#define R_STABLE_COUNT           3           /* 连续稳定次数 = 3×10ms = 30ms */
#define R_SG_THRESHOLD           0           /* SG_RESULT 堵转检测关闭: 仅 stst+MSCNT */
#define R_SG_MIN_SPEED           800         /* (保留, 已无效) */
#define R_STUCK_MS               200         /* MSCNT 不动超时判定卡死 (兜底) */
#define R_TIMEOUT_MS             8000        /* 整体旋转超时 (ms) */
#define R_UART_MAX_FAILS         3           /* 连续 UART 失败上限 */
/* ================================================================
 *  标定数据 — W25Q64 Flash 持久化
 * ================================================================ */

/* 存储位置: W25Q64 最后一个 4KB 扇区 (8MB - 4KB) */
#define CALIB_FLASH_ADDR  0x7FF000
#define CALIB_FLASH_SIZE  4096

/* Magic 值: "PNP\0" 按小端序存储为 uint32 */
#define CALIB_MAGIC       0x504E5080U

/*
 * 标定数据结构 — 写入 Flash 前计算 CRC32 并填入 crc32 字段。
 * CRC 覆盖范围: 从 magic 到 cam_p3_val_to_steps (不含 crc32 自身)。
 */
typedef struct {
    uint32_t magic;               /* CALIB_MAGIC，标识有效标定数据 */

    /* ---- 散料区 ---- */
    int32_t  scatter_x_steps;     /* 散料区原点 X (步数) */
    int32_t  scatter_y_steps;     /* 散料区原点 Y (步数) */
    int32_t  scatter_size_steps;  /* 散料区边长 (步数) */

    /* ---- 加热台平台区域 ---- */
    int32_t  heat_platform_x_min;      /* 加热台左下 X (步数) */
    int32_t  heat_platform_y_min;      /* 加热台左下 Y (步数) */
    int32_t  heat_platform_x_max;      /* 加热台右上 X (步数) */
    int32_t  heat_platform_y_max;      /* 加热台右上 Y (步数) */

    /* ---- 下相机位置 ---- */
    int32_t  bottom_cam_x_steps;  /* 下相机 X 坐标 (步数) */
    int32_t  bottom_cam_y_steps;  /* 下相机 Y 坐标 (步数) */

    /* ---- 上摄像头 à 吸嘴偏置 ---- */
    int32_t  cam_to_nozzle_dx_steps; /* 摄像头à吸嘴 X 偏置 (步数): 标定时 step2 - step1 */
    int32_t  cam_to_nozzle_dy_steps; /* 摄像头à吸嘴 Y 偏置 (步数): 标定时 step2 - step1 */

    /* ---- Z 轴三高度 (舵机角度 deg) ---- */
    float    z_safe_angle;        /* 安全高度: XY 运动时保持 */
    float    z_pick_angle;        /* 吸取高度: 吸嘴接触散料区元件 */
    float    z_place_angle;       /* 贴装高度: 元件接触 PCB 焊盘 */

    /* ---- 摄像头像素 à 步数比例 ---- */
    float    cam_p1_val_to_steps; /* P1 (上摄像头) 视觉值 à 步数 */
    float    cam_p3_val_to_steps; /* P3 (下摄像头) 视觉值 à 步数 */

    /* ---- CRC32 校验 ---- */
    uint32_t crc32;               /* CRC32 of bytes [magic .. cam_to_nozzle_dy_steps] */
} CalibrationData_t;

/*
 * 默认标定值 — 首次上电 magic 不匹配时使用。
 * 所有坐标字段为 0，Z 角度使用代码中的硬编码默认值。
 */
#define CALIB_DEFAULT_Z_SAFE      78.0f
#define CALIB_DEFAULT_Z_PICK     116.0f
#define CALIB_DEFAULT_Z_PLACE    116.0f
//#define CALIB_DEFAULT_CAM_P1      3.277f
//#define CALIB_DEFAULT_CAM_P3      3.277f

/* ---- 全局标定实例 (定义在 app_host.c) ---- */
extern CalibrationData_t g_calib;

/* ---- Flash 读写 (实现在 driver_spiflash_w25q64.c) ---- */
int Calib_Save(const CalibrationData_t *calib);
int Calib_Load(CalibrationData_t *calib);


#endif