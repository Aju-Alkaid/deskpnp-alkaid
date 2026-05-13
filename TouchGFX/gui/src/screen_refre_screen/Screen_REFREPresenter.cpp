#include <gui/screen_refre_screen/Screen_REFREView.hpp>
#include <gui/screen_refre_screen/Screen_REFREPresenter.hpp>

Screen_REFREPresenter::Screen_REFREPresenter(Screen_REFREView& v)
    : view(v)
{

}

void Screen_REFREPresenter::activate()
{

}

void Screen_REFREPresenter::deactivate()
{

}

Model * Screen_REFREPresenter::getModel()
{
    return model;
}

void Screen_REFREPresenter::onKeyPressed(uint8_t keyId)
{
    // 调用View的更新函数，而非直接操作UI
    view.handleKeyEvent(keyId);
}