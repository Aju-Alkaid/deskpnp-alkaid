#include <gui/screen_import_screen/Screen_IMPORTView.hpp>
#include "key.h"
#include "gui/model/Data_Transfer.h"
#include <stdio.h>

Screen_IMPORTView::Screen_IMPORTView()
    : lastDownloadState(0xFF)
{

}

void Screen_IMPORTView::setupScreen()
{
    Screen_IMPORTViewBase::setupScreen();

    // TODO: 向上位机发送就绪信号（需确保串口已初始化）
    // printf("DOWNLOAD_READY\r\n");

    // 初始化显示状态
    if_DOWNLOAD_READY = 0;
    Ready.setVisible(true);
    Ready.invalidate();
    Importing.setVisible(false);
    Importing.invalidate();
    Import_done.setVisible(false);
    Import_done.invalidate();

    lastDownloadState = 0;
}

void Screen_IMPORTView::tearDownScreen()
{
    Screen_IMPORTViewBase::tearDownScreen();
}

void Screen_IMPORTView::handleTickEvent()
{
    uint8_t state = if_DOWNLOAD_READY;

    if (state == lastDownloadState) return;

    switch (state) {
    case 0:
        Ready.setVisible(true);
        Ready.invalidate();
        Importing.setVisible(false);
        Importing.invalidate();
        Import_done.setVisible(false);
        Import_done.invalidate();
        break;

    case 1:
        Ready.setVisible(false);
        Ready.invalidate();
        Importing.setVisible(true);
        Importing.invalidate();
        Import_done.setVisible(false);
        Import_done.invalidate();
        break;

    case 2:
        Ready.setVisible(false);
        Ready.invalidate();
        Importing.setVisible(false);
        Importing.invalidate();
        Import_done.setVisible(true);
        Import_done.invalidate();
        break;
    }

    lastDownloadState = state;
}

void Screen_IMPORTView::handleKeyEvent(uint8_t key)
{
    // 导入完成时按下 PUSH -> 返回 HOME 并开始贴片
    if (if_DOWNLOAD_READY == 2 && key == KEY_PUSH) {
        smt_Start();   // TODO: 主程序实现贴片逻辑
        static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_HOMEScreenNoTransition();
        return;
    }

    // 其他情况走通用按键处理
    pageTable.handleKey(key);
}