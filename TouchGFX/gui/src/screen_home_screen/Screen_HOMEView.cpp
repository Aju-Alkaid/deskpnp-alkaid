#include <gui/screen_home_screen/Screen_HOMEView.hpp>
#include <gui/common/FrontendApplication.hpp>

Screen_HOMEView::Screen_HOMEView()
{

}

void Screen_HOMEView::setupScreen()
{
    Screen_HOMEViewBase::setupScreen();
}

void Screen_HOMEView::tearDownScreen()
{
    Screen_HOMEViewBase::tearDownScreen();
}

void Screen_HOMEView::handleTickEvent()
{

}

void Screen_HOMEView::handleKeyEvent(uint8_t key)
{
    // 将按键事件传递给 PageTable 容器
		pageTable.handleKey(key);
}