#ifndef SCREEN_HOMEPRESENTER_HPP
#define SCREEN_HOMEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Screen_HOMEView;

class Screen_HOMEPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    Screen_HOMEPresenter(Screen_HOMEView& v);

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

    virtual ~Screen_HOMEPresenter() {}
    Model * getModel();
    virtual void onKeyPressed(uint8_t keyId);
private:
    Screen_HOMEPresenter();

    Screen_HOMEView& view;
};

#endif // SCREEN_HOMEPRESENTER_HPP
