#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#include <stdint.h>

/* ---- 共享运动控制常量 ---- */
#define STEPS_PER_MM     3276.8f
#define JOG_MAX_STEPS    8388607
#define X1_ADDR          0x01
#define X2_ADDR          0x02
#define Y_ADDR           0x03

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

    /* ---- PCB 扫描区域 ---- */
    int32_t  pcb_area_x_min;      /* PCB 区域左下 X (步数) */
    int32_t  pcb_area_y_min;      /* PCB 区域左下 Y (步数) */
    int32_t  pcb_area_x_max;      /* PCB 区域右上 X (步数) */
    int32_t  pcb_area_y_max;      /* PCB 区域右上 Y (步数) */

    /* ---- 下相机位置 ---- */
    int32_t  bottom_cam_x_steps;  /* 下相机 X 坐标 (步数) */
    int32_t  bottom_cam_y_steps;  /* 下相机 Y 坐标 (步数) */

    /* ---- Z 轴三高度 (舵机角度 deg) ---- */
    float    z_safe_angle;        /* 安全高度: XY 运动时保持 */
    float    z_pick_angle;        /* 吸取高度: 吸嘴接触散料区元件 */
    float    z_place_angle;       /* 贴装高度: 元件接触 PCB 焊盘 */

    /* ---- 摄像头像素 à 步数比例 ---- */
    float    cam_p1_val_to_steps; /* P1 (上摄像头) 视觉值 à 步数 */
    float    cam_p3_val_to_steps; /* P3 (下摄像头) 视觉值 à 步数 */

    /* ---- CRC32 校验 ---- */
    uint32_t crc32;               /* CRC32 of bytes [magic .. cam_p3_val_to_steps] */
} CalibrationData_t;

/*
 * 默认标定值 — 首次上电 magic 不匹配时使用。
 * 所有坐标字段为 0，Z 角度使用代码中的硬编码默认值。
 */
#define CALIB_DEFAULT_Z_SAFE     120.0f
#define CALIB_DEFAULT_Z_PICK      60.0f
#define CALIB_DEFAULT_Z_PLACE     55.0f
#define CALIB_DEFAULT_CAM_P1      0.003f
#define CALIB_DEFAULT_CAM_P3      0.001f

/* ---- 全局标定实例 (定义在 app_host.c) ---- */
extern CalibrationData_t g_calib;

/* ---- Flash 读写 (实现在 driver_spiflash_w25q64.c) ---- */
int Calib_Save(const CalibrationData_t *calib);
int Calib_Load(CalibrationData_t *calib);


#endif