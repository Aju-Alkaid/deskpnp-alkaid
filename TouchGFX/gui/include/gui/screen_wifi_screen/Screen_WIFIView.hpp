#ifndef SCREEN_WIFIVIEW_HPP
#define SCREEN_WIFIVIEW_HPP

#include <gui_generated/screen_wifi_screen/Screen_WIFIViewBase.hpp>
#include <gui/screen_wifi_screen/Screen_WIFIPresenter.hpp>

class Screen_WIFIView : public Screen_WIFIViewBase
{
public:
    Screen_WIFIView();
    virtual ~Screen_WIFIView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleKeyEvent(uint8_t key);
protected:
};

#endif // SCREEN_WIFIVIEW_HPP

