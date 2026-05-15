/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
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
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "driver_can.h"
#include "app_motion.h"
#include "app_test.h"
#include "driver_tmc2209.h"
#include "tmc_protocol.h"
#include "driver_drv8803.h"
#include "app_vision.h"
#include "app_host.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
osMessageQueueId_t motor_event_queue; 
osMessageQueueId_t can_rx_queue;
osMessageQueueId_t motion_cmd_queue;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
osThreadId_t PnPMotionTaskHandle; // 閿熸枻鎷烽敓鏂ゆ嫹閿熸枻????
osThreadId_t motorTestTaskHandle;
osThreadId_t drv8803TestTaskHandle;
osThreadId_t servoTestTaskHandle;
osThreadId_t mksHandle;

const osThreadAttr_t PnP_Motion_Task_attributes = { 
  .name = "PnPMotion",
  .stack_size = 256 * 4,  // 鏍堥敓鏂ゆ嫹灏忛敓鏂ゆ嫹閿熻鑺傦綇????
  .priority = (osPriority_t)osPriorityNormal, // 閿熸枻鎷烽敓楗虹》????
};

//鐢垫満娴嬭瘯浠诲姟
const osThreadAttr_t motorTestTask_attributes = {
  .name = "MotorTest",
  .stack_size = 512,          // 鍫嗘爤缁欏ぇ涓€鐐癸紝鍥犱负鐢ㄤ簡 printf
  .priority = (osPriority_t) osPriorityNormal,
};

//8803娴嬭瘯浠诲姟
const osThreadAttr_t drv8803TestTask_attributes = {
  .name = "DRV8803_Test",
  .stack_size = 512,           // ??????小
  .priority = osPriorityNormal  // ?????????
};

//鑸垫満娴嬭瘯浠诲姟
const osThreadAttr_t servoTestTask_attributes = {
    .name = "ServoTest",
    .stack_size = 514,                // ????
    .priority = osPriorityNormal,
};

//MKS娴嬭瘯浠诲姟
const osThreadAttr_t MKSTestTask_attributes = {
    .name = "MKSTest",
    .stack_size = 1024,                // MKS motor
    .priority = osPriorityNormal,
};

//澶勭悊 CAN 鎶ユ枃鐨勭嫭绔嬩换锟?
const osThreadAttr_t hostTask_attributes = {
    .name = "HostComm",
    .stack_size = 1024,
    .priority = osPriorityNormal
};

const osThreadAttr_t canProcTask_attr = { 
    .name = "CAN_Proc", 
    .stack_size = 512, 
    .priority = osPriorityNormal 
};


// 锟斤拷位锟斤拷通讯锟斤拷锟斤拷锟斤拷锟斤拷
const osThreadAttr_t hostTestTask_attributes = {
    .name = "HostTest",
    .stack_size = 512,
    .priority = osPriorityNormal
};

/* 锟斤拷位锟斤拷通讯 + XY 锟剿讹拷锟斤拷锟狡诧拷锟斤拷锟斤拷锟斤拷 锟斤拷锟斤拷 */
const osThreadAttr_t hostMotionTestTask_attributes = {
    .name = "HostMotion",
    .stack_size = 2048,
    .priority = osPriorityNormal
};

/* USER CODE END Variables */
/* Definitions for touchGFX */
osThreadId_t touchGFXHandle;
const osThreadAttr_t touchGFX_attributes = {
  .name = "touchGFX",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 2048 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void StartUartTestTask(void *argument); // 闁跨喐鏋婚幏鐑芥晸閺傘倖瀚圭紓鍝勩亼闁跨喍鑼庨悮瀛樺闁跨喐鏋婚敓????
void StartMotorTestTask(void *argument); // 闁跨喐鏋婚幏鐑芥晸閺傘倖瀚圭紓鍝勩亼闁跨喍鑼庨悮瀛樺闁跨喐鏋婚敓????
void PnP_Motion_Task(void *argument);    // 闁跨喐鏋婚幏鐑芥晸閺傘倖瀚圭紓鍝勩亼闁跨喍鑼庨悮瀛樺闁跨喐鏋婚敓????
void StartDrv8803TestTask(void *argument); 
void StartServoTestTask(void *argument);   
void vMotorTestTask(void *pvParameters) ;
void CAN_Process_Task(void *argument);
void Host_Task(void *argument);
void StartHostCommTestTask(void *argument);
void StartHostMotionTestTask(void *argument);
/* USER CODE END FunctionPrototypes */

void TouchGFX_Task(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
	UART_Driver_Init();
	Semaphore_Init();
	Event_Init();
	Vision_Init();
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */	
	
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
 
  can_rx_queue = osMessageQueueNew(10, sizeof(CAN_Rx_Packet_t), NULL);
	motor_event_queue = osMessageQueueNew(32, sizeof(CAN_Rx_Packet_t), NULL);
	motion_cmd_queue = osMessageQueueNew(20, sizeof(MotionCmd_t), NULL);
	
	
	
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of touchGFX */
  touchGFXHandle = osThreadNew(TouchGFX_Task, NULL, &touchGFX_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
	TaskHandle_t UartTestTaskHandle = NULL;
//	xTaskCreate(StartUartTestTask, "UartTest", 256, NULL, tskIDLE_PRIORITY + 2, &UartTestTaskHandle);

//	osThreadNew(PnP_Motion_Task, NULL, &PnP_Motion_Task_attributes);

//	drv8803TestTaskHandle = osThreadNew(StartDrv8803TestTask, NULL, &drv8803TestTask_attributes);
  
//	motorTestTaskHandle = osThreadNew(StartMotorTestTask, NULL, &motorTestTask_attributes);

//  servoTestTaskHandle = osThreadNew(StartServoTestTask, NULL, &servoTestTask_attributes);

	osThreadNew(CAN_Process_Task, NULL, &canProcTask_attr);

//    osThreadNew(Host_Task, NULL, &hostTask_attributes);

//    mksHandle = osThreadNew(vMotorTestTask, NULL, &MKSTestTask_attributes);

    // 锟斤拷位锟斤拷通讯+锟剿讹拷锟斤拷锟斤拷锟斤拷锟斤拷
  osThreadNew(StartHostMotionTestTask, NULL, &hostMotionTestTask_attributes);


  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_TouchGFX_Task */
/**
  * @brief  Function implementing the touchGFX thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_TouchGFX_Task */
__weak void TouchGFX_Task(void *argument)
{
  /* USER CODE BEGIN TouchGFX_Task */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END TouchGFX_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */


/* USER CODE END Application */

