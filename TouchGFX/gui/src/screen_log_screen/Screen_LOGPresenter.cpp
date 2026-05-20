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
    // 调用View的更新函数，而非直接操作UI
    view.handleKeyEvent(keyId);
}