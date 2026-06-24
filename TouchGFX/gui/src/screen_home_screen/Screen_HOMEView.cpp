#include <gui/screen_home_screen/Screen_HOMEView.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <gui/model/Data_Transfer.h>


Screen_HOMEView::Screen_HOMEView()
    : last_speed_applied(0)
    , last_motor_speed(0)
    , last_if_now_SMT(0xFF)
    , last_total_SMT(0xFF)
    , last_now_SMT(0xFF)
    , last_Temp(0xFF)

{

}

void Screen_HOMEView::setupScreen()
{
    Screen_HOMEViewBase::setupScreen();

    // 缁戝畾娓╁害鏂囨湰鐨?wildcard 缂撳啿鍖?
    temperature.setWildcard(wildcard1Buffer);

    // 鍒濆鍖栧畬鎴愬悗绔嬪嵆鍒锋柊涓€娆★紝鏄剧ず鍒濆鐘舵€?
    applyMotorSpeed(last_motor_speed, true);
    handleFreshEvent();

    // 鏇存柊缂撳瓨
    last_if_now_SMT = if_now_SMT;
    last_total_SMT = total_SMT;
    last_now_SMT   = now_SMT;
    last_Temp      = Temp;
}

void Screen_HOMEView::tearDownScreen()
{
    Screen_HOMEViewBase::tearDownScreen();
}

void Screen_HOMEView::handleTickEvent()
{
    // 浠呭湪 Data_Transfer 鏁版嵁鍙樺寲鏃舵墠鍒锋柊灞忓箷
    if (if_now_SMT != last_if_now_SMT ||
        total_SMT  != last_total_SMT  ||
        now_SMT    != last_now_SMT    ||
        Temp       != last_Temp)
    {
        handleFreshEvent();

        last_if_now_SMT = if_now_SMT;
        last_total_SMT  = total_SMT;
        last_now_SMT    = now_SMT;
        last_Temp       = Temp;
    }
}

void Screen_HOMEView::handleSMTProgress(uint8_t current, uint8_t total)
{
    // 高频进度不走全局变量，避免耦合旧逻辑
    now_SMT = current;
    total_SMT = total;
    if_now_SMT = 1;
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

    // 鍒锋柊娓╁害鏁版嵁
    touchgfx::Unicode::snprintf(wildcard1Buffer, sizeof(wildcard1Buffer)/sizeof(wildcard1Buffer[0]), "%u", Temp);
    temperature.invalidate();
}





