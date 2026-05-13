#ifndef SCREEN_REFREPRESENTER_HPP
#define SCREEN_REFREPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Screen_REFREView;

class Screen_REFREPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    Screen_REFREPresenter(Screen_REFREView& v);

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

    virtual ~Screen_REFREPresenter() {}
    Model * getModel();
    virtual void onKeyPressed(uint8_t keyId);
private:
    Screen_REFREPresenter();

    Screen_REFREView& view;
};

#endif // SCREEN_REFREPRESENTER_HPP
