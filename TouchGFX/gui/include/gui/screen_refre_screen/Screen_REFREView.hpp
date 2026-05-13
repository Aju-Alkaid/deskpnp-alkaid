#ifndef SCREEN_REFREVIEW_HPP
#define SCREEN_REFREVIEW_HPP

#include <gui_generated/screen_refre_screen/Screen_REFREViewBase.hpp>
#include <gui/screen_refre_screen/Screen_REFREPresenter.hpp>

class Screen_REFREView : public Screen_REFREViewBase
{
public:
    Screen_REFREView();
    virtual ~Screen_REFREView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
     
		virtual	void handleKeyEvent(uint8_t key);
protected:
};

#endif // SCREEN_REFREVIEW_HPP
