/**
  ******************************************************************************
  * @file           pid_controller.h
  * @brief          通用PID控制器头文件 (已修复逻辑错误)
  * @version        1.1.0 (Fixed)
  ******************************************************************************
  */

#ifndef __PID_CONTROLLER_H
#define __PID_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* 确保结构体4字节对齐，防止Cortex-M4F出现HardFault */
#ifndef __ALIGNED
  #if defined(__CC_ARM)
    #define __ALIGNED(x) __align(x)
  #elif defined(__ICCARM__)
    #define __ALIGNED(x) _Alignas(x)
  #elif defined(__GNUC__)
    #define __ALIGNED(x) __attribute__((aligned(x)))
  #else
    /* 默认空定义，防止编译错误 */
    #define __ALIGNED(x)
  #endif
#endif

#if defined(USE_FREERTOS)
#include "cmsis_os.h"
#endif

typedef enum
{
  PID_MODE_POSITION = 0,
  PID_MODE_INCREMENTAL
} PID_Mode_t;

typedef enum
{
  PID_STATE_STOP = 0,
  PID_STATE_RUN
} PID_State_t;

/* 4字节对齐的结构体定义 */
typedef struct __ALIGNED(4)
{
  /* --- 配置参数 --- */
  float Kp;
  float Ki;
  float Kd;
  
  float MaxOutput;
  float MaxIntegral;
  float DeadZone;
  
  /* 高级功能 */
  float IntegralSeparationThreshold;//积分分离阈值
  uint8_t EnableIntegralSeparation;//启用整数分离
  
  float SetpointFilterAlpha; /* 0=No Filter, 1=Strong Filter */
  uint8_t EnableDerivativeOnMeasurement; /* 1=测量的导数 (Recommended) */ //在测量时启动导数功能
  
  PID_Mode_t Mode;

  /* --- 内部状态变量 --- */
  float Setpoint;
  float SetpointFiltered;
  float Measurement;
  
  float Error;
  float ErrorPrev;
  float ErrorPrev2;
  
  /* 用于微分先行的测量值历史数据 */
  float MeasurementPrev; 
  
  float Integral;
  float Output;
  float OutputPrev;
  
  PID_State_t State;

  /* --- 线程安全 --- */
  #if defined(USE_FREERTOS)
  osMutexId_t MutexId; /* 使用 osMutexId_t 类型 */
  #endif

} PID_Controller_t;

/* 接口函数 */
static float ConstrainFloat(float val, float min, float max);
void PID_Init(PID_Controller_t *pid, PID_Mode_t mode);
float PID_Compute(PID_Controller_t *pid, float measurement);
void PID_SetParams(PID_Controller_t *pid, float kp, float ki, float kd);
void PID_SetSetpoint(PID_Controller_t *pid, float sp);
void PID_SetState(PID_Controller_t *pid, PID_State_t state);
void PID_Reset(PID_Controller_t *pid);
float PID_GetError(PID_Controller_t *pid);
float PID_GetIntegral(PID_Controller_t *pid);


#ifdef __cplusplus
}
#endif

#endif /* __PID_CONTROLLER_H */