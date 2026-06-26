#include <gui/screen_wifi_screen/Screen_WIFIView.hpp>

Screen_WIFIView::Screen_WIFIView()
{

}

void Screen_WIFIView::setupScreen()
{
    Screen_WIFIViewBase::setupScreen();
}

void Screen_WIFIView::tearDownScreen()
{
    Screen_WIFIViewBase::tearDownScreen();
}

void Screen_WIFIView::handleKeyEvent(uint8_t key)
{
    pageTable.handleKey(key);
}

