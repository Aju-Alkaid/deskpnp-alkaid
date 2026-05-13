#include <gui/screen_log_screen/Screen_LOGView.hpp>
#include "key.h"

Screen_LOGView::Screen_LOGView()
{

}

void Screen_LOGView::setupScreen()
{
    Screen_LOGViewBase::setupScreen();
}

void Screen_LOGView::tearDownScreen()
{
    Screen_LOGViewBase::tearDownScreen();
}

void Screen_LOGView::handleKeyEvent(uint8_t key)
{
    // 将按键事件传递给 PageTable 容器
		pageTable.handleKey(key);
}