/**
  ******************************************************************************
  * @file           pid_controller.c
  * @brief          PID控制器核心算法实现 (已修复微分先行与积分分离逻辑)
  ******************************************************************************
  */

#include "pid.h"
#include <string.h>

/* 辅助函数：限幅 */
static float ConstrainFloat(float val, float min, float max)
{
  if (val > max) return max;
  if (val < min) return min;
  return val;
}

/**
  * @brief 初始化PID控制器
  */
void PID_Init(PID_Controller_t *pid, PID_Mode_t mode)
{
  if (pid == NULL) return;

  /* 清零内存 */
  memset(pid, 0, sizeof(PID_Controller_t));

  /* 默认参数 */
  pid->Kp = 1.0f;
  pid->Ki = 0.5f;
  pid->Kd = 0.1f;
  pid->MaxOutput = 1000.0f;
  pid->MaxIntegral = 500.0f;
  pid->DeadZone = 0.0f;
  
  pid->IntegralSeparationThreshold = 100.0f;
  pid->EnableIntegralSeparation = 1;
  
  pid->SetpointFilterAlpha = 0.0f;
  /* 默认开启微分先行，这是工业控制的最佳实践 */
  pid->EnableDerivativeOnMeasurement = 1; 

  pid->Mode = mode;
  pid->State = PID_STATE_STOP;

  /* FreeRTOS 互斥锁初始化 (仅在RTOS启动后调用) */
  #if defined(USE_FREERTOS)
  /* 检查系统调度器是否已启动，防止在初始化阶段调用导致错误 */
  if (osKernelGetState() == osKernelRunning)
  {
    osMutexAttr_t attr = { .name = "PidMutex" };
    pid->MutexId = osMutexNew(&attr);
  }
  #endif
}

/**
  * @brief 核心计算 (最终修正版)
  */
float PID_Compute(PID_Controller_t *pid, float measurement)
{
  if (pid == NULL) return 0.0f;

  /* --- 线程安全与中断保护 --- */
  bool is_mutex_taken = false;
  #if defined(USE_FREERTOS)
  if (pid->MutexId != NULL)
  {
    /* 检测是否在中断中调用。如果在中断中，不能调用 osMutexAcquire */
    /* __get_PRIMASK() 返回 1 表示中断关闭，或者通过 osThreadGetId() == NULL 判断 */
    if (osThreadGetId() != NULL) 
    {
        osMutexAcquire(pid->MutexId, osWaitForever);
        is_mutex_taken = true;
    }
  }
  #endif

  if (pid->State == PID_STATE_STOP) goto COMPUTE_EXIT;

  /* 1. 更新反馈 */
  pid->Measurement = measurement;

  /* 2. 设定值滤波 (一阶低通) */
  if (pid->SetpointFilterAlpha > 0.0f && pid->SetpointFilterAlpha < 1.0f)
  {
    pid->SetpointFiltered = pid->SetpointFilterAlpha * pid->Setpoint + 
                            (1.0f - pid->SetpointFilterAlpha) * pid->SetpointFiltered;
  }
  else
  {
    pid->SetpointFiltered = pid->Setpoint;
  }

  /* 3. 误差计算 */
  pid->Error = pid->SetpointFiltered - pid->Measurement;

  /* 4. 死区处理 */
  if (fabsf(pid->Error) < pid->DeadZone)
  {
    pid->Error = 0.0f;
  }

  /* 5. 积分项处理 */
  /* 修正点：增量式模式下，不应累加积分项，防止变量溢出 */
  if (pid->Mode == PID_MODE_POSITION)
  {
    if (!pid->EnableIntegralSeparation || fabsf(pid->Error) < pid->IntegralSeparationThreshold)
    {
      pid->Integral += pid->Error;
      pid->Integral = ConstrainFloat(pid->Integral, -pid->MaxIntegral, pid->MaxIntegral);
    }
    /* 如果触发积分分离，Integral 保持不变 (保持上一时刻值) */
  }
  else
  {
    /* 增量式模式：积分项不参与计算，也不累加，保持为0或用于监控 */
    /* 这里选择重置为0，避免混淆 */
    pid->Integral = 0.0f; 
  }

  /* 6. 微分项处理 */
  float derivative_term = 0.0f;
  
  /* 修正点：微分先行仅在位置式模式下有效且推荐。
     在增量式模式下，为了数学一致性，回退到标准误差微分，
     或者使用专门的增量式微分公式。这里为了稳健性，
     若为增量式，强制使用标准微分。 */
  bool use_measurement_derivative = (pid->EnableDerivativeOnMeasurement && (pid->Mode == PID_MODE_POSITION));

  if (use_measurement_derivative)
  {
    /* 微分先行：D = Kd * (Measurement[n-1] - Measurement[n]) */
    /* 注意：这里不需要乘 dt，因为 Kd 已经包含了时间因子 */
    derivative_term = pid->Kd * (pid->MeasurementPrev - pid->Measurement);
  }
  else
  {
    /* 标准微分：D = Kd * (Error[n] - Error[n-1]) */
    derivative_term = pid->Kd * (pid->Error - pid->ErrorPrev);
  }

  /* 7. 输出计算 */
  if (pid->Mode == PID_MODE_POSITION)
  {
    /* 位置式：P + I + D */
    pid->Output = (pid->Kp * pid->Error) + (pid->Ki * pid->Integral) + derivative_term;
  }
  else
  {
    /* 增量式：Delta = P + I + D */
    /* 注意：这里的 P, I, D 对应增量公式的各项 */
    float delta_p = pid->Kp * (pid->Error - pid->ErrorPrev);
    float delta_i = pid->Ki * pid->Error; /* 增量式积分项直接用 Ki*Error */
    float delta_d = pid->Kd * (pid->Error - 2.0f * pid->ErrorPrev + pid->ErrorPrev2);
    
    pid->Output = pid->Output + delta_p + delta_i + delta_d;
  }

  /* 8. 输出限幅 */
  pid->Output = ConstrainFloat(pid->Output, -pid->MaxOutput, pid->MaxOutput);

COMPUTE_EXIT:
  /* 9. 更新历史数据 */
  pid->ErrorPrev2 = pid->ErrorPrev;
  pid->ErrorPrev = pid->Error;
  pid->MeasurementPrev = pid->Measurement;

  /* 释放锁 */
  #if defined(USE_FREERTOS)
  if (is_mutex_taken) osMutexRelease(pid->MutexId);
  #endif

  return pid->Output;
}

/* 其他辅助函数保持不变... */
void PID_SetParams(PID_Controller_t *pid, float kp, float ki, float kd)
{
  if (pid == NULL) return;
  #if defined(USE_FREERTOS)
  if (pid->MutexId) osMutexAcquire(pid->MutexId, osWaitForever);
  #endif
  pid->Kp = kp; pid->Ki = ki; pid->Kd = kd;
  #if defined(USE_FREERTOS)
  if (pid->MutexId) osMutexRelease(pid->MutexId);
  #endif
}

void PID_SetSetpoint(PID_Controller_t *pid, float sp)
{
  if (pid == NULL) return;
  #if defined(USE_FREERTOS)
  if (pid->MutexId) osMutexAcquire(pid->MutexId, osWaitForever);
  #endif
  pid->Setpoint = sp;
  #if defined(USE_FREERTOS)
  if (pid->MutexId) osMutexRelease(pid->MutexId);
  #endif
}

void PID_SetState(PID_Controller_t *pid, PID_State_t state)
{
  if (pid == NULL) return;
  pid->State = state;
  if (state == PID_STATE_STOP) 
		PID_Reset(pid);/* 停止时通常建议重置 */
}
/* --- 辅助函数实现 --- */
void PID_Reset(PID_Controller_t *pid)
{
  if (pid == NULL) return;
  pid->Integral = 0.0f;
  pid->Error = 0.0f;
  pid->ErrorPrev = 0.0f;
  pid->ErrorPrev2 = 0.0f;
  pid->Output = 0.0f;
  pid->OutputPrev = 0.0f;
  pid->MeasurementPrev = pid->Measurement; /* 关键：防止第一次微分计算跳变 */
	
}

float PID_GetError(PID_Controller_t *pid)
{
  return pid ? pid->Error : 0.0f;
}

float PID_GetIntegral(PID_Controller_t *pid)
{
  return pid ? pid->Integral : 0.0f;
}