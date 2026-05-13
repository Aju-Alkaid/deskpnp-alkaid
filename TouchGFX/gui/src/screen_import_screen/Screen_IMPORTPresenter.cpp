#include <gui/screen_import_screen/Screen_IMPORTView.hpp>
#include <gui/screen_import_screen/Screen_IMPORTPresenter.hpp>

Screen_IMPORTPresenter::Screen_IMPORTPresenter(Screen_IMPORTView& v)
    : view(v)
{

}

void Screen_IMPORTPresenter::activate()
{

}

void Screen_IMPORTPresenter::deactivate()
{

}

Model * Screen_IMPORTPresenter::getModel()
{
    return model;
}

void Screen_IMPORTPresenter::onKeyPressed(uint8_t keyId)
{
    // 调用View的更新函数，而非直接操作UI
    view.handleKeyEvent(keyId);
}