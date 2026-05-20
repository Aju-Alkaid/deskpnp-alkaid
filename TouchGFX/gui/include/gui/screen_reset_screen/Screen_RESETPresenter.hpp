#ifndef SCREEN_RESETPRESENTER_HPP
#define SCREEN_RESETPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Screen_RESETView;

class Screen_RESETPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    Screen_RESETPresenter(Screen_RESETView& v);

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

    virtual ~Screen_RESETPresenter() {}
		Model * getModel();
    virtual void onKeyPressed(uint8_t keyId);

private:
    Screen_RESETPresenter();

    Screen_RESETView& view;
};

#endif // SCREEN_RESETPRESENTER_HPP
