#ifndef SCREEN_IMPORTVIEW_HPP
#define SCREEN_IMPORTVIEW_HPP

#include <gui_generated/screen_import_screen/Screen_IMPORTViewBase.hpp>
#include <gui/screen_import_screen/Screen_IMPORTPresenter.hpp>

class Screen_IMPORTView : public Screen_IMPORTViewBase
{
public:
    Screen_IMPORTView();
    virtual ~Screen_IMPORTView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

    virtual void handleKeyEvent(uint8_t key);
protected:
    uint8_t lastDownloadState;
};

#endif // SCREEN_IMPORTVIEW_HPP