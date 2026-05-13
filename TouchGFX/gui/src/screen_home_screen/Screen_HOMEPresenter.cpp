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
    // 调用View的更新函数，而非直接操作UI
    view.handleKeyEvent(keyId);
}
