#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <platform/driver/button/ButtonController.hpp>
#include "key.h"
#include "stm32g4xx_hal.h"
Model::Model() : modelListener(0)
{

}

void Model::tick()
{
    static uint32_t last_scan_tick = 0;
    uint32_t now = HAL_GetTick();
    if (now - last_scan_tick >= 10) {
        last_scan_tick = now;
        Key_Scan();
    }

    for (int i = 0; i < KEY_NUM; i++) {
        if (Key_GetEvent(i)) {
            modelListener->onKeyPressed(i);
            Key_ClearEvent(i);
        }
    }
}

    