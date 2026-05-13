#include <gui/screen_refre_screen/Screen_REFREView.hpp>
#include "key.h"

Screen_REFREView::Screen_REFREView()
{

}

void Screen_REFREView::setupScreen()
{
    Screen_REFREViewBase::setupScreen();
}

void Screen_REFREView::tearDownScreen()
{
    Screen_REFREViewBase::tearDownScreen();
}

void Screen_REFREView::handleKeyEvent(uint8_t key)
{
    // 将按键事件传递给 PageTable 容器
		pageTable.handleKey(key);
}