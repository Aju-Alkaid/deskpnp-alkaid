#ifndef SCREEN_HOMEVIEW_HPP
#define SCREEN_HOMEVIEW_HPP

#include <gui_generated/screen_home_screen/Screen_HOMEViewBase.hpp>
#include <gui/screen_home_screen/Screen_HOMEPresenter.hpp>
//#include "key.h"



class Screen_HOMEView : public Screen_HOMEViewBase
{
public:
    Screen_HOMEView();
    virtual ~Screen_HOMEView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
 
		virtual	void handleKeyEvent(uint8_t key);
protected:
};

#endif // SCREEN_HOMEVIEW_HPP
