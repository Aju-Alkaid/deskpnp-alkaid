#include <KeyController.hpp>

using namespace touchgfx;

void KeyController::init()
{
}

uint8_t KeyController::getKey()
{
	return Key_IsAnyPressed();
}

bool KeyController::sample(uint8_t& key)
{
	// 从消抖队列取按键松开事件（单击触发），避免与 Model 竞争
	if (keyEventQueue != NULL) {
		KeyEvent_t evt;
		if (osMessageQueueGet(keyEventQueue, &evt, NULL, 0) == osOK) {
			if (evt.type == 0) {
				key = evt.key_id;
				return true;
			}
		}
	}
	return false;
}