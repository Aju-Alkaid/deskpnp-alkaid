/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SPI4_CS_Pin GPIO_PIN_3
#define SPI4_CS_GPIO_Port GPIOE
#define C3RESET_Pin GPIO_PIN_13
#define C3RESET_GPIO_Port GPIOC
#define TEMP_DAT_Pin GPIO_PIN_9
#define TEMP_DAT_GPIO_Port GPIOF
#define NRST_Pin GPIO_PIN_10
#define NRST_GPIO_Port GPIOG
#define TEMP_DATA3_Pin GPIO_PIN_3
#define TEMP_DATA3_GPIO_Port GPIOA
#define NENBLE2_Pin GPIO_PIN_4
#define NENBLE2_GPIO_Port GPIOA
#define NFAULT2_Pin GPIO_PIN_5
#define NFAULT2_GPIO_Port GPIOA
#define IN5_Pin GPIO_PIN_6
#define IN5_GPIO_Port GPIOA
#define IN6_Pin GPIO_PIN_7
#define IN6_GPIO_Port GPIOA
#define IN7_Pin GPIO_PIN_4
#define IN7_GPIO_Port GPIOC
#define IN8_Pin GPIO_PIN_5
#define IN8_GPIO_Port GPIOC
#define RESET2_Pin GPIO_PIN_0
#define RESET2_GPIO_Port GPIOB
#define C224V_Pin GPIO_PIN_1
#define C224V_GPIO_Port GPIOB
#define C124V_Pin GPIO_PIN_2
#define C124V_GPIO_Port GPIOB
#define C212V_Pin GPIO_PIN_8
#define C212V_GPIO_Port GPIOE
#define NENBLE1_Pin GPIO_PIN_9
#define NENBLE1_GPIO_Port GPIOE
#define RESET1_Pin GPIO_PIN_10
#define RESET1_GPIO_Port GPIOE
#define IN4_Pin GPIO_PIN_11
#define IN4_GPIO_Port GPIOE
#define IN3_Pin GPIO_PIN_12
#define IN3_GPIO_Port GPIOE
#define IN2_Pin GPIO_PIN_13
#define IN2_GPIO_Port GPIOE
#define IN1_Pin GPIO_PIN_14
#define IN1_GPIO_Port GPIOE
#define NFAULT1_Pin GPIO_PIN_15
#define NFAULT1_GPIO_Port GPIOE
#define C112V_Pin GPIO_PIN_10
#define C112V_GPIO_Port GPIOB
#define LCD_SCK_Pin GPIO_PIN_13
#define LCD_SCK_GPIO_Port GPIOB
#define LCD_MOSI_Pin GPIO_PIN_15
#define LCD_MOSI_GPIO_Port GPIOB
#define LCD_RESET_Pin GPIO_PIN_8
#define LCD_RESET_GPIO_Port GPIOD
#define LCD_DC_RS_Pin GPIO_PIN_9
#define LCD_DC_RS_GPIO_Port GPIOD
#define LCD_CS_Pin GPIO_PIN_10
#define LCD_CS_GPIO_Port GPIOD
#define TMC2_EN_Pin GPIO_PIN_14
#define TMC2_EN_GPIO_Port GPIOD
#define TMC1_EN_Pin GPIO_PIN_15
#define TMC1_EN_GPIO_Port GPIOD
#define KEY1_Pin GPIO_PIN_6
#define KEY1_GPIO_Port GPIOC
#define KEY2_Pin GPIO_PIN_7
#define KEY2_GPIO_Port GPIOC
#define CCW_Pin GPIO_PIN_8
#define CCW_GPIO_Port GPIOC
#define PUSH_Pin GPIO_PIN_9
#define PUSH_GPIO_Port GPIOC
#define CW_Pin GPIO_PIN_8
#define CW_GPIO_Port GPIOA
#define SPI3_CS_Pin GPIO_PIN_15
#define SPI3_CS_GPIO_Port GPIOA
#define BOOT0_Pin GPIO_PIN_8
#define BOOT0_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
