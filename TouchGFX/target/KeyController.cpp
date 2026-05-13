#include <KeyController.hpp>


using namespace touchgfx;

void KeyController::init()
{
	Key_Init();
}

uint8_t KeyController::getKey()
{
	return Key_IsAnyPressed();
}

bool KeyController::sample(uint8_t & key)	//采样
{
	uint8_t pressedKey = KeyController::getKey(); 
	if (pressedKey != 0) 
	{
        key = pressedKey; 
        return true;  // 表示有按键按下，TouchGFX会记录这个值
    }
        return false; // 表示无按键按下
}


