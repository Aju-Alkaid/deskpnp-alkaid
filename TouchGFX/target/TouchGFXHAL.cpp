/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : TouchGFXHAL.cpp
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
/* USER CODE END Header */

#include <TouchGFXHAL.hpp>

/* USER CODE BEGIN TouchGFXHAL.cpp */
#include "lcd.h"
#include "spi.h"
#include <touchgfx/hal/OSWrappers.hpp>
#include <string.h> 
#include <KeyController.hpp>

using namespace touchgfx;

extern "C" void touchgfxSignalVSync(void);

extern "C" {
    uint8_t isInited = 0;
}

// ---- TIM7 VSYNC ----
extern TIM_HandleTypeDef htim7;

void TouchGFX_VSYNC_TimerInit(void)
{
    HAL_TIM_Base_Stop_IT(&htim7);
    htim7.Init.Period = 33333 - 1;
    HAL_TIM_Base_Init(&htim7);
    HAL_TIM_Base_Start_IT(&htim7);
}

extern "C" void TouchGFX_VSYNC_TimerStart(void)
{
    HAL_TIM_Base_Start_IT(&htim7);
}

extern "C" void TouchGFX_VSYNC_IRQCallback(void)
{
    if (isInited) {
        touchgfxSignalVSync();
    }
}

static KeyController keyController;

void TouchGFXHAL::initialize()
{
    TouchGFXGeneratedHAL::initialize(); 
    setButtonController(&keyController);
    keyController.init();
    isInited = 1;
}

uint16_t* TouchGFXHAL::getTFTFrameBuffer() const
{
    return TouchGFXGeneratedHAL::getTFTFrameBuffer();
}

void TouchGFXHAL::setTFTFrameBuffer(uint16_t* address)
{
    TouchGFXGeneratedHAL::setTFTFrameBuffer(address);
}

// 辅助函数
// 将 TouchGFX 帧缓冲区（1bpp 水平扫描）转换为驱动所需的 2×4 格式
static void convert_1bpp_to_2x4(const uint8_t* src, uint8_t* dst, uint16_t width, uint16_t height)
{
    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint16_t src_byte_idx = y * ((width + 7) / 8) + (x / 8);
            uint8_t src_bit = 7 - (x % 8);
            uint8_t color = (src[src_byte_idx] >> src_bit) & 0x01;
            uint16_t dst_byte_idx = (y / 2) * (width / 4) + (x / 4);
            uint8_t dst_bit = 7 - ((x % 4) * 2 + (y % 2));
            if (color)
                dst[dst_byte_idx] |= (1 << dst_bit);
            else
                dst[dst_byte_idx] &= ~(1 << dst_bit);
        }
    }
}

void TouchGFXHAL::flushFrameBuffer(const touchgfx::Rect& rect) {
    const uint8_t* fb = (const uint8_t*)getClientFrameBuffer();
    if (fb == nullptr) return;
    convert_1bpp_to_2x4(fb, display_buffer, LCD_W, LCD_H);
    ST7306_Refresh();
}

extern "C" 
void touchgfxSignalVSync()
{
    if (isInited)
    {
        touchgfx::HAL::getInstance()->vSync();
        touchgfx::OSWrappers::signalVSync();
    }
}

bool TouchGFXHAL::blockCopy(void* RESTRICT dest, const void* RESTRICT src, uint32_t numBytes)
{
    return TouchGFXGeneratedHAL::blockCopy(dest, src, numBytes);
}

void TouchGFXHAL::configureInterrupts()
{
    TouchGFXGeneratedHAL::configureInterrupts();
}

void TouchGFXHAL::enableInterrupts()
{
    TouchGFXGeneratedHAL::enableInterrupts();
}

void TouchGFXHAL::disableInterrupts()
{
    TouchGFXGeneratedHAL::disableInterrupts();
}

void TouchGFXHAL::enableLCDControllerInterrupt()
{
    TouchGFX_VSYNC_TimerInit();
    HAL_NVIC_SetPriority(TIM7_DAC_IRQn, 14, 0);
    HAL_NVIC_EnableIRQ(TIM7_DAC_IRQn);
}

bool TouchGFXHAL::beginFrame()
{
    return TouchGFXGeneratedHAL::beginFrame();
}

void TouchGFXHAL::endFrame()
{
    TouchGFXGeneratedHAL::endFrame();
}

/* USER CODE END TouchGFXHAL.cpp */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
