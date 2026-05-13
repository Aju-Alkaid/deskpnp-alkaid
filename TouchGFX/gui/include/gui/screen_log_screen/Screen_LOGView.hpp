#ifndef SCREEN_LOGVIEW_HPP
#define SCREEN_LOGVIEW_HPP

#include <gui_generated/screen_log_screen/Screen_LOGViewBase.hpp>
#include <gui/screen_log_screen/Screen_LOGPresenter.hpp>

class Screen_LOGView : public Screen_LOGViewBase
{
public:
    Screen_LOGView();
    virtual ~Screen_LOGView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
     
		virtual	void handleKeyEvent(uint8_t key);
protected:
};

#endif // SCREEN_LOGVIEW_HPP
