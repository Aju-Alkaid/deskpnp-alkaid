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

    // WiFi 状态切换（由 Presenter 回调或内部 KEY2 触发）
    void setWifiEnabled(bool enabled);
    bool isWifiEnabled() const { return m_wifiEnabled; }

protected:
    bool m_wifiEnabled;

    // 更新 ON/OFF 图片显示
    void applyWifiImage();
};

#endif // SCREEN_WIFIVIEW_HPP
