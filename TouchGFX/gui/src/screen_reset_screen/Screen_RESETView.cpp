#include <gui/screen_reset_screen/Screen_RESETView.hpp>
#include "key.h"

Screen_RESETView::Screen_RESETView()
{

}

void Screen_RESETView::setupScreen()
{
    Screen_RESETViewBase::setupScreen();

    // 注册控件引用到 PageTable（使用基类的 Reseting / Reset_done）
    pageTable.setRefreWidgets(caution, Reseting, Reset_done);
}

void Screen_RESETView::tearDownScreen()
{
    Screen_RESETViewBase::tearDownScreen();
}

void Screen_RESETView::handleTickEvent()
{
    pageTable.refre_tick();
}

void Screen_RESETView::handleKeyEvent(uint8_t key)
{
    PageTable::RefreState state = pageTable.getRefreState();

    switch (state) {
    case PageTable::REFRE_NORMAL:
        pageTable.refre_handleKey(key);
        break;

    case PageTable::REFRE_RESETING:
        // 复位中：屏蔽所有按键
        break;

    case PageTable::REFRE_DONE:
        // 复位完成：仅响应 UP/DOWN
        if (key == KEY_UP) {
            Reset_done.setVisible(false);
            Reset_done.invalidate();
            pageTable.setRefreState(PageTable::REFRE_NORMAL);
            static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_HOMEScreenNoTransition();
        } else if (key == KEY_DOWN) {
            Reset_done.setVisible(false);
            Reset_done.invalidate();
            pageTable.setRefreState(PageTable::REFRE_NORMAL);
            static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_IMPORTScreenNoTransition();
        }
        // 其他按键忽略
        break;
    }
}

void Screen_RESETView::reset_screen_update()
{

}