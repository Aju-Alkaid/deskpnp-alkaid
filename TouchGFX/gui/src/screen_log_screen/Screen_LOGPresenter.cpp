#include <gui/screen_log_screen/Screen_LOGView.hpp>
#include <gui/screen_log_screen/Screen_LOGPresenter.hpp>

Screen_LOGPresenter::Screen_LOGPresenter(Screen_LOGView& v)
    : view(v)
{

}

void Screen_LOGPresenter::activate()
{

}

void Screen_LOGPresenter::deactivate()
{

}

Model * Screen_LOGPresenter::getModel()
{
    return model;
}

void Screen_LOGPresenter::onKeyPressed(uint8_t keyId)
{
    // 璋冪敤View鐨勬洿鏂板嚱鏁帮紝鑰岄潪鐩存帴鎿嶄綔UI
    view.handleKeyEvent(keyId);
}

void Screen_LOGPresenter::onNotifyLogText(uint8_t code, uint8_t param)
{
    view.handleSystemLog(code, param);
}
