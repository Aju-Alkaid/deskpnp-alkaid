#ifndef SCREEN_IMPORTPRESENTER_HPP
#define SCREEN_IMPORTPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Screen_IMPORTView;

class Screen_IMPORTPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    Screen_IMPORTPresenter(Screen_IMPORTView& v);

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

    virtual ~Screen_IMPORTPresenter() {}
    Model * getModel();
    virtual void onKeyPressed(uint8_t keyId);

private:
    Screen_IMPORTPresenter();

    Screen_IMPORTView& view;
};

#endif // SCREEN_IMPORTPRESENTER_HPP
