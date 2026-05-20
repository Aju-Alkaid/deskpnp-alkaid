/**
  ******************************************************************************
  * File Name          : app_touchgfx.c
  ******************************************************************************
  * This file was created by TouchGFX Generator 4.22.1. This file is only
  * generated once! Delete this file from your project and re-generate code
  * using STM32CubeMX or change this file manually to update it.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app_touchgfx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lcd.h"
#include "spi.h"
#include "cmsis_os2.h"
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/

/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void touchgfx_init(void);
void touchgfx_components_init(void);
void touchgfx_taskEntry(void);
extern void TouchGFX_VSYNC_TimerStart(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/**
 * PreOS Initialization function
 */
void MX_TouchGFX_PreOSInit(void)
{
}

/**
 * Initialize TouchGFX application
 */
void MX_TouchGFX_Init(void)
{
  touchgfx_components_init();
  touchgfx_init();
}

/**
 * TouchGFX application entry function
 */
void MX_TouchGFX_Process(void)
{
  touchgfx_taskEntry();
}

/**
 * TouchGFX application thread
 */
	
void TouchGFX_Task(void *argument)
{
  // LCD 硬件初始化（必须在 RTOS 启动后执行，HAL tick 已运行）
  ST7306_Init(&hspi2);
  // 进入 TouchGFX 主渲染循环（永不返回，VSYNC 由 HAL 内部启动）
  touchgfx_taskEntry();
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/