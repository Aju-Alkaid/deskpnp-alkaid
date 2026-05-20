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

    virtual void handleKeyEvent(uint8_t key); // 处理按键函数
protected:
    touchgfx::Unicode::UnicodeChar wildcard1Buffer[10];

    // 缓存上次刷新的值，仅在变化时刷新
    uint8_t  last_if_now_SMT;
    uint8_t  last_total_SMT;
    uint8_t  last_now_SMT;
    uint8_t  last_Temp;
    bool increase = true;
};

#endif // SCREEN_HOMEVIEW_HPP