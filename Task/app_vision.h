#ifndef __APP_VISION_H
#define __APP_VISION_H

#include <stdint.h>
#include <stdbool.h>

/* 摄像头返回数据类型 */
typedef enum {
    CAM_NONE = 0,
    CAM_PROC1_OK,       /* process1 成功: 元件信息 */
    CAM_PROC1_ERR,      /* process1 失败 */
    CAM_PROC2_OK,       /* process2 成功: Mark点 */
    CAM_PROC2_ERR,      /* process2 失败 */
    CAM_PROC3_OK,       /* process3 成功: 元件偏移 */
    CAM_PROC3_ERR,      /* process3 失败 */
} CamResult_t;

/* 摄像头返回数据 */
typedef struct {
    CamResult_t result;
    /* process1: 元件检测 */
    int32_t x_offset;       /* X轴偏移 */
    int32_t y_offset;       /* Y轴偏移 */
    int32_t comp_info;      /* 元件信息 */
    /* process2: Mark点 */
    int32_t mark1_x;        /* 圆1 X坐标 */
    int32_t mark1_y;        /* 圆1 Y坐标 */
    int32_t mark2_x;        /* 圆2 X坐标 */
    int32_t mark2_y;        /* 圆2 Y坐标 */
} CamData_t;

/* 摄像头命令类型 */
typedef enum {
    CAM_CMD_PROC1,      /* 上位摄像头检测元件 */
    CAM_CMD_PROC2,      /* 上位摄像头检测Mark点 */
    CAM_CMD_PROC3,      /* 下位摄像头检测元件偏移 */
} CamCmd_t;

void Vision_Init(void);
void CamUart_RecvCallback(uint8_t *data, int len);
void Vision_SendCmd(CamCmd_t cmd);

#endif