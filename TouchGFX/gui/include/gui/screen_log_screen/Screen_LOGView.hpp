#ifndef SCREEN_LOGVIEW_HPP
#define SCREEN_LOGVIEW_HPP

#include <gui_generated/screen_log_screen/Screen_LOGViewBase.hpp>
#include <gui/screen_log_screen/Screen_LOGPresenter.hpp>
#include <touchgfx/Unicode.hpp>

class Screen_LOGView : public Screen_LOGViewBase
{
public:
    Screen_LOGView();
    virtual ~Screen_LOGView() {}
    virtual void setupScreen();
    virtual void handleTickEvent();
    virtual void tearDownScreen();
    virtual void LogAddStr(const char* str);

    virtual void handleKeyEvent(uint8_t key);

protected:
    static const uint16_t LOG_BUF_SIZE = 2048;
    touchgfx::Unicode::UnicodeChar logBuffer[LOG_BUF_SIZE];
    uint16_t logBufferUsed;
};

#endif // SCREEN_LOGVIEW_HPP