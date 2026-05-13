#include <gui/screen_import_screen/Screen_IMPORTView.hpp>
#include "key.h"

Screen_IMPORTView::Screen_IMPORTView()
{

}

void Screen_IMPORTView::setupScreen()
{
    Screen_IMPORTViewBase::setupScreen();
}

void Screen_IMPORTView::tearDownScreen()
{
    Screen_IMPORTViewBase::tearDownScreen();
}

void Screen_IMPORTView::handleKeyEvent(uint8_t key)
{
    // 将按键事件传递给 PageTable 容器
		pageTable.handleKey(key);
}
