#include <gui/screen_home_screen/Screen_HOMEView.hpp>
#include <gui/screen_home_screen/Screen_HOMEPresenter.hpp>

Screen_HOMEPresenter::Screen_HOMEPresenter(Screen_HOMEView& v)
    : view(v)
{

}

void Screen_HOMEPresenter::activate()
{

}

void Screen_HOMEPresenter::deactivate()
{

}

Model * Screen_HOMEPresenter::getModel()
{
    return model;
}

void Screen_HOMEPresenter::onKeyPressed(uint8_t keyId)
{
    // 璋冪敤View鐨勬洿鏂板嚱鏁帮紝鑰岄潪鐩存帴鎿嶄綔UI
    view.handleKeyEvent(keyId);
}

void Screen_HOMEPresenter::onNotifySMTProgress(uint8_t current, uint8_t total)
{
    view.handleSMTProgress(current, total);
}

void Screen_HOMEPresenter::onNotifyMotorSpeed(uint16_t speed)
{
    view.handleMotorSpeed(speed);
}

void Screen_HOMEPresenter::onNotifyTemp(uint16_t temp)
{
    view.handleTemp(temp);
}

void Screen_HOMEPresenter::onNotifySMTStatus(uint8_t is_smt)
{
    view.handleSMTStatus(is_smt);
}

void Screen_HOMEPresenter::onNotifyWifiStatus(uint8_t connected)
{
    view.handleWifiStatus(connected);
}

