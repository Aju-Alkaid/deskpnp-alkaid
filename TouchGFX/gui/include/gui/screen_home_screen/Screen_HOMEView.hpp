#ifndef SCREEN_HOMEVIEW_HPP
#define SCREEN_HOMEVIEW_HPP

#include <gui_generated/screen_home_screen/Screen_HOMEViewBase.hpp>
#include <gui/screen_home_screen/Screen_HOMEPresenter.hpp>
#include <touchgfx/Unicode.hpp>
#include <stdint.h>
#include "key.h"



class Screen_HOMEView : public Screen_HOMEViewBase
{
public:
    Screen_HOMEView();
    virtual ~Screen_HOMEView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent(); // 每tick调用一次
    virtual void handleFreshEvent();// 处理刷新事件函数
    void applyMotorSpeed(uint16_t speed, bool force = false);

    virtual void handleKeyEvent(uint8_t key); // 处理按键函数
    virtual void handleSMTProgress(uint8_t current, uint8_t total);
    virtual void handleMotorSpeed(uint16_t speed);
    virtual void handleTemp(uint16_t temp);
    virtual void handleSMTStatus(uint8_t is_smt);
    virtual void handleWifiStatus(uint8_t connected);
protected:
    static const uint16_t MOTOR_SPEED_SIZE = 10;
    touchgfx::Unicode::UnicodeChar motorSpeedBuffer[MOTOR_SPEED_SIZE];
    static const uint16_t WILDCARD1_SIZE = 10;
    touchgfx::Unicode::UnicodeChar wildcard1Buffer[WILDCARD1_SIZE];

    // 缓存上次刷新的值，仅在变化时刷新
    uint8_t  last_speed_applied;
    uint16_t last_motor_speed;
};

#endif // SCREEN_HOMEVIEW_HPP

