#include <gui/screen_wifi_screen/Screen_WIFIView.hpp>
#include <gui/model/Data_Transfer.h>
#include "key.h"

Screen_WIFIView::Screen_WIFIView()
    : m_wifiEnabled(false)
{
}

void Screen_WIFIView::setupScreen()
{
    Screen_WIFIViewBase::setupScreen();

    // 初始化 ON/OFF 图片状态
    applyWifiImage();
}

void Screen_WIFIView::tearDownScreen()
{
    Screen_WIFIViewBase::tearDownScreen();
}

void Screen_WIFIView::handleKeyEvent(uint8_t key)
{
    if (key == KEY_KEY1) {
        // 切换 WiFi 开关
        setWifiEnabled(!m_wifiEnabled);
    } else {
        // 其他按键交给 PageTable 处理（CW/CCW 翻页等）
        pageTable.handleKey(key);
    }
}

void Screen_WIFIView::setWifiEnabled(bool enabled)
{
    m_wifiEnabled = enabled;

    // 更新图片显示
    applyWifiImage();

    // 通过消息路由发送 WiFi 控制命令给主控
    DT_Msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = DT_CMD_WIFI_CTRL;
    msg.data.status = enabled ? 1 : 0;
    DT_SendCommand(&msg);
}

void Screen_WIFIView::applyWifiImage()
{
    if (m_wifiEnabled) {
        ON.setVisible(true);
        OFF.setVisible(false);
    } else {
        ON.setVisible(false);
        OFF.setVisible(true);
    }
    ON.invalidate();
    OFF.invalidate();
}
