#include <gui/screen_home_screen/Screen_HOMEView.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <gui/model/Data_Transfer.h>


Screen_HOMEView::Screen_HOMEView()
    : last_speed_applied(0)
    , last_motor_speed(0)

{

}

void Screen_HOMEView::setupScreen()
{
    Screen_HOMEViewBase::setupScreen();

    // 缁戝畾娓╁害鏂囨湰鐨?wildcard 缂撳啿鍖?
    temperature.setWildcard(wildcard1Buffer);

    // 鍒濆鍖栧畬鎴愬悗绔嬪嵆鍒锋柊涓€娆★紝鏄剧ず鍒濆鐘舵€?
    touchgfx::Unicode::snprintf(wildcard1Buffer, sizeof(wildcard1Buffer)/sizeof(wildcard1Buffer[0]), "--");
    temperature.invalidate();
}

void Screen_HOMEView::tearDownScreen()
{
    Screen_HOMEViewBase::tearDownScreen();
}

void Screen_HOMEView::handleTickEvent()
{
    // 通知驱动模式：所有显示更新由 Presenter 回调驱动，不再轮询全局变量
}

void Screen_HOMEView::handleSMTProgress(uint8_t current, uint8_t total)
{
    (void)current;
    (void)total;
    Progress1.Data_Refresh();
}


void Screen_HOMEView::handleMotorSpeed(uint16_t speed)
{
    applyMotorSpeed(speed, false);
}

void Screen_HOMEView::handleKeyEvent(uint8_t key)
{
    // 灏嗘寜閿簨浠朵紶閫掔粰 PageTable 瀹瑰櫒
    pageTable.handleKey(key);
}

void Screen_HOMEView::applyMotorSpeed(uint16_t speed, bool force)
{
    if (!force && speed == last_motor_speed) return;
    last_motor_speed = speed;

    touchgfx::Unicode::snprintf(motorSpeedBuffer, sizeof(motorSpeedBuffer) / sizeof(motorSpeedBuffer[0]), "%u", speed);
    MotorSpeed.setWildcard(motorSpeedBuffer);
    MotorSpeed.invalidate();
    last_speed_applied = 1;
}

void Screen_HOMEView::handleFreshEvent()
{
    // 鍒锋柊璐寸墖杩涘害
    Progress1.Data_Refresh();
}

void Screen_HOMEView::handleTemp(uint16_t temp)
{
    touchgfx::Unicode::snprintf(wildcard1Buffer, sizeof(wildcard1Buffer)/sizeof(wildcard1Buffer[0]), "%u", temp);
    temperature.invalidate();
}

void Screen_HOMEView::handleSMTStatus(uint8_t is_smt)
{
    if (!is_smt) {
        // 璐寸墖缁撴潫锛氳繘搴﹀綊闆?
        Progress1.Data_Refresh();
    }
}

void Screen_HOMEView::handleWifiStatus(uint8_t connected)
{
    // 棰勭暀锛歐iFi 鐘舵€佸浘鏍囧垏鎹?
    (void)connected;
}
