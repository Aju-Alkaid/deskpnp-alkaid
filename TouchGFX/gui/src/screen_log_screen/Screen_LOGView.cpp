#include <gui/screen_log_screen/Screen_LOGView.hpp>
#include <gui/model/Data_Transfer.h>
#include "key.h"
#include <texts/TextKeysAndLanguages.hpp>
#include <stdio.h>
#include <string.h>

Screen_LOGView::Screen_LOGView()
    : logBufferUsed(0)
    , m_autoFollow(true)
    , m_lastScrollY(0)
    , m_focused(false)
{
    logBuffer[0] = 0;
}

void Screen_LOGView::setupScreen()
{
    Screen_LOGViewBase::setupScreen();

    // 启用自动换行
    textArea1.setWideTextAction(touchgfx::WIDE_TEXT_WORDWRAP);

    // 将日志缓冲区绑定到 textArea1 的 wildcard
    textArea1.setWildcard(logBuffer);

    // 初始跟随最新日志
    m_autoFollow = true;
    m_lastScrollY = 0;
    m_focused = false;

    // 仅首次启动写入模拟日志，后续重进页面不再重复写入
    static bool s_firstBoot = true;
    if (s_firstBoot) {
        s_firstBoot = false;
        LogAddStr("=== PnP System Boot ===");
        LogAddStr("Initializing peripherals...");
        LogAddStr("  GPIO       [OK]");
        LogAddStr("  SPI Flash  [OK]");
        LogAddStr("  Motor X    [OK]");
        LogAddStr("  Motor Y    [OK]");
        LogAddStr("  Motor Z    [OK]");
        LogAddStr("  Temp Sensor[OK]");
        LogAddStr("System ready.");
    }

    // 刷新显示当前缓冲区内容
    textArea1.invalidate();
    scrollToBottom(true);
}

void Screen_LOGView::handleTickEvent()
{
    // 检测用户是否手动上滑，若是则暂停自动跟随
    const int16_t currentY = scrollableContainer1.getScrolledY();
    const int16_t maxY = scrollableContainer1.getHeight() - scrollableContainer1.getScrolledY();
    const int16_t delta = currentY - m_lastScrollY;

    if (delta < -4) {
        m_autoFollow = false;
    } else if (maxY >= 0 && currentY >= maxY - 4) {
        m_autoFollow = true;
    }

    m_lastScrollY = currentY;
}

void Screen_LOGView::tearDownScreen()
{
    Screen_LOGViewBase::tearDownScreen();
}

void Screen_LOGView::handleKeyEvent(uint8_t key)
{
    if (!m_focused) {
        if (key == KEY_PUSH) {
            m_focused = true;
        }
        pageTable.handleKey(key);
        return;
    }

    switch (key) {
        case KEY_UP:
            m_autoFollow = false;
            scrollableContainer1.doScroll(0, -40);
            break;
        case KEY_DOWN:
            m_autoFollow = false;
            scrollableContainer1.doScroll(0, 40);
            break;
        case KEY_KEY1:
            m_autoFollow = true;
            scrollToBottom(true);
            break;
        case KEY_KEY2:
            logBufferUsed = 0;
            logBuffer[0] = 0;
            m_autoFollow = true;
            textArea1.setWildcard(logBuffer);
            textArea1.setHeight(scrollableContainer1.getHeight());
            textArea1.invalidate();
            break;
        case KEY_PUSH:
            m_focused = false;
            break;
        default:
            break;
    }
}

void Screen_LOGView::handleSystemLog(uint8_t code, uint8_t param)
{
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%u:%u", (unsigned)code, (unsigned)param);
    LogAddStr(tmp);
}

bool Screen_LOGView::isUserScrolledUp() const
{
    return !m_autoFollow;
}

bool Screen_LOGView::isNearBottom() const
{
    return m_autoFollow;
}

void Screen_LOGView::scrollToBottom(bool force)
{
    if (force || m_autoFollow || isNearBottom()) {
        scrollableContainer1.invalidate();
        scrollableContainer1.doScroll(0, 9999);
        m_autoFollow = true;
    }
}

uint16_t Screen_LOGView::countLines() const
{
    uint16_t count = 0;
    for (uint16_t i = 0; i < logBufferUsed; ++i) {
        if (logBuffer[i] == '\n') {
            count++;
        }
    }
    return count;
}

void Screen_LOGView::trimOldestLines(uint16_t maxLines)
{
    uint16_t lines = countLines();
    while (lines > maxLines) {
        uint16_t removePos = 0;
        while (removePos < logBufferUsed && logBuffer[removePos] != '\n') {
            removePos++;
        }
        if (removePos < logBufferUsed) {
            removePos++; // 包含换行符
        }

        const uint16_t remain = logBufferUsed - removePos;
        if (remain > 0) {
            memmove(&logBuffer[0], &logBuffer[removePos], remain * sizeof(logBuffer[0]));
        }
        logBufferUsed = remain;
        logBuffer[logBufferUsed] = 0;
        lines--;
    }
}

void Screen_LOGView::LogAddStr(const char* str)
{
    if (str == NULL || str[0] == '\0') {
        return;
    }

    uint16_t strLen = (uint16_t)strlen(str);

    // 预裁剪：保留 8 行余量，避免每条新日志都触发裁剪
    trimOldestLines(LOG_MAX_LINES + LOG_LINE_RESERVE);

    // 检查缓冲区空间: 新字符串 + 换行符 + 终止符
    if (logBufferUsed + strLen + 2 > LOG_BUF_SIZE)
        return;

    // 将 ASCII 字符串逐字符拷贝到 Unicode 缓冲区
    for (uint16_t i = 0; i < strLen; i++) {
        logBuffer[logBufferUsed + i] = (touchgfx::Unicode::UnicodeChar)str[i];
    }
    logBufferUsed += strLen;

    // 添加换行符
    logBuffer[logBufferUsed++] = '\n';
    logBuffer[logBufferUsed] = 0;

    // 刷新显示
    textArea1.setWildcard(logBuffer);

    // 根据文本高度调整 textArea1 高度（不小于初始 600）
    uint16_t textH = textArea1.getTextHeight();
    if (textH > textArea1.getHeight()) {
        textArea1.setHeight(textH + 10);
    }

    textArea1.invalidate();
    scrollToBottom(false);
}



