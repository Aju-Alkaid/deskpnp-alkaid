#ifndef SCREEN_LOGPRESENTER_HPP
#define SCREEN_LOGPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Screen_LOGView;

class Screen_LOGPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    Screen_LOGPresenter(Screen_LOGView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~Screen_LOGPresenter() {}
    Model * getModel();
    virtual void onKeyPressed(uint8_t keyId);

private:
    Screen_LOGPresenter();

    Screen_LOGView& view;
};

#endif // SCREEN_LOGPRESENTER_HPP
