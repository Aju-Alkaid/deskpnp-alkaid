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
#include "key.h"
#include "gui/model/Data_Transfer.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
osMessageQueueId_t motor_event_queue; 
osMessageQueueId_t motion_cmd_queue;
extern osMessageQueueId_t keyEventQueue;
extern osMessageQueueId_t dataTransferQueue;
osMutexId_t g_debug_mutex = NULL;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
osThreadId_t PnPMotionTaskHandle; 
osThreadId_t motorTestTaskHandle;
osThreadId_t drv8803TestTaskHandle;
osThreadId_t servoTestTaskHandle;
osThreadId_t mksHandle;
osThreadId_t hostMotionTaskHandle;

const osThreadAttr_t PnP_Motion_Task_attributes = { 
  .name = "PnPMotion",
  .stack_size = 256 * 4,  
  .priority = (osPriority_t)osPriorityNormal, 
};


const osThreadAttr_t motorTestTask_attributes = {
  .name = "MotorTest",
  .stack_size = 512,          
  .priority = (osPriority_t) osPriorityNormal,
};


const osThreadAttr_t drv8803TestTask_attributes = {
  .name = "DRV8803_Test",
  .stack_size = 1024,           
  .priority = osPriorityNormal  
};


const osThreadAttr_t servoTestTask_attributes = {
    .name = "ServoTest",
    .stack_size = 514,                
    .priority = osPriorityNormal,
};


const osThreadAttr_t MKSTestTask_attributes = {
    .name = "MKSTest",
    .stack_size = 1024,                // MKS motor
    .priority = osPriorityNormal,
};


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



const osThreadAttr_t hostTestTask_attributes = {
    .name = "HostTest",
    .stack_size = 512,
    .priority = osPriorityNormal
};


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

/* Definitions for KeyTask */
const osThreadAttr_t keyTask_attributes = {
  .name = "KeyTask",
 .priority = (osPriority_t) osPriorityNormal,
 .stack_size = 256
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void StartUartTestTask(void *argument);   
void StartMotorTestTask(void *argument); 
void PnP_Motion_Task(void *argument);    
void StartDrv8803TestTask(void *argument); 
void StartServoTestTask(void *argument);   
void vMotorTestTask(void *pvParameters) ;
void CAN_Process_Task(void *argument);
void Host_Task(void *argument);
void StartHostCommTestTask(void *argument);
void StartHostMotionTestTask(void *argument);
void Key_Task(void *argument);
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
	Event_Init();
	Vision_Init();
	Key_Init();
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  g_debug_mutex = osMutexNew(NULL);
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */	
	
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
 
	motor_event_queue = osMessageQueueNew(32, sizeof(CAN_Rx_Packet_t), NULL);
	motion_cmd_queue = osMessageQueueNew(20, sizeof(MotionCmd_t), NULL);
	keyEventQueue = osMessageQueueNew(16, sizeof(KeyEvent_t), NULL);
	dataTransferQueue = osMessageQueueNew(16, sizeof(DT_Msg_t), NULL);
	host_pkt_queue = osMessageQueueNew(16, sizeof(HostMsg_t), NULL);
	
	
	
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of touchGFX */
  touchGFXHandle = osThreadNew(TouchGFX_Task, NULL, &touchGFX_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
	TaskHandle_t UartTestTaskHandle = NULL;
//	xTaskCreate(StartUartTestTask, "UartTest", 256, NULL, tskIDLE_PRIORITY + 2, &UartTestTaskHandle);

//	osThreadNew(PnP_Motion_Task, NULL, &PnP_Motion_Task_attributes);

	drv8803TestTaskHandle = osThreadNew(StartDrv8803TestTask, NULL, &drv8803TestTask_attributes);
  
	motorTestTaskHandle = osThreadNew(StartMotorTestTask, NULL, &motorTestTask_attributes);

//  servoTestTaskHandle = osThreadNew(StartServoTestTask, NULL, &servoTestTask_attributes);

	osThreadNew(CAN_Process_Task, NULL, &canProcTask_attr);
	osThreadNew(Key_Task, NULL, &keyTask_attributes);

//    osThreadNew(Host_Task, NULL, &hostTask_attributes);

//    mksHandle = osThreadNew(vMotorTestTask, NULL, &MKSTestTask_attributes);

  
  osThreadNew(StartHostMotionTestTask, NULL, &hostMotionTestTask_attributes);
  hostMotionTaskHandle = osThreadNew(StartHostMotionTestTask, NULL, &hostMotionTestTask_attributes);

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_TouchGFX_Task */
extern void touchgfx_taskEntry();
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
  touchgfx_taskEntry();
  /* USER CODE END TouchGFX_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */


/* USER CODE END Application */

