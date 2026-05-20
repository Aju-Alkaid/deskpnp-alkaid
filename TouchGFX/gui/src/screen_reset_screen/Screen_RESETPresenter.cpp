#include <gui/screen_reset_screen/Screen_RESETView.hpp>
#include <gui/screen_reset_screen/Screen_RESETPresenter.hpp>

Screen_RESETPresenter::Screen_RESETPresenter(Screen_RESETView& v)
    : view(v)
{

}

void Screen_RESETPresenter::activate()
{

}

void Screen_RESETPresenter::deactivate()
{

}

Model * Screen_RESETPresenter::getModel()
{
    return model;
}

void Screen_RESETPresenter::onKeyPressed(uint8_t keyId)
{
    // 调用View的更新函数，而非直接操作UI
    view.handleKeyEvent(keyId);
}