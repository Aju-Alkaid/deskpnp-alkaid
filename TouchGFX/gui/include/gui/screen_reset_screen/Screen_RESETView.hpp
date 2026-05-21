#ifndef SCREEN_RESETVIEW_HPP
#define SCREEN_RESETVIEW_HPP

#include <gui_generated/screen_reset_screen/Screen_RESETViewBase.hpp>
#include <gui/screen_reset_screen/Screen_RESETPresenter.hpp>

class Screen_RESETView : public Screen_RESETViewBase
{
public:
    Screen_RESETView();
    virtual ~Screen_RESETView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
    virtual void reset_screen_update();

    virtual void handleKeyEvent(uint8_t key);
protected:
};

#endif // SCREEN_RESETVIEW_HPP