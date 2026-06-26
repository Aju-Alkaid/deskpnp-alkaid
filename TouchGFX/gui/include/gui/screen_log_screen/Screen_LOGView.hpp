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
    bool isUserScrolledUp() const;
    bool isNearBottom() const;
    void scrollToBottom(bool force = false);
    void trimOldestLines(uint16_t maxLines);
    uint16_t countLines() const;

    virtual void handleKeyEvent(uint8_t key);
    virtual void handleSystemLog(uint8_t code, uint8_t param);
    bool isFocused() const { return m_focused; }

protected:
    static const uint16_t LOG_MAX_LINES = 100;
    static const uint16_t LOG_LINE_RESERVE = 8;
    static const uint16_t LOG_BUF_SIZE = 2048;
    touchgfx::Unicode::UnicodeChar logBuffer[LOG_BUF_SIZE];
    uint16_t logBufferUsed;
    bool m_autoFollow;
    int16_t m_lastScrollY;
    bool m_focused;
};

#endif // SCREEN_LOGVIEW_HPP





