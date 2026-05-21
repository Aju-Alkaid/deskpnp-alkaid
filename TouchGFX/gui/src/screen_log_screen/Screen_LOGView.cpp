#include <gui/screen_log_screen/Screen_LOGView.hpp>
#include <gui/model/Data_Transfer.h>
#include "key.h"
#include <texts/TextKeysAndLanguages.hpp>
#include <stdio.h>
#include <string.h>

Screen_LOGView::Screen_LOGView()
    : logBufferUsed(0)
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

    // 写入模拟日志
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

void Screen_LOGView::handleTickEvent()
{

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

void Screen_LOGView::LogAddStr(const char* str)
{
    uint16_t strLen = (uint16_t)strlen(str);

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

    // 滚动到底部 (doScroll 会自动 clamp 到有效范围)
    scrollableContainer1.doScroll(0, 9999);
}