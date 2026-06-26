#ifndef FRONTENDAPPLICATION_HPP
#define FRONTENDAPPLICATION_HPP

#include <gui_generated/common/FrontendApplicationBase.hpp>

class FrontendHeap;

using namespace touchgfx;

class FrontendApplication : public FrontendApplicationBase
{
public:
    FrontendApplication(Model& m, FrontendHeap& heap);
    virtual ~FrontendApplication() { }

    virtual void handleTickEvent()
    {
        model.tick();
        FrontendApplicationBase::handleTickEvent();
    }

    // WIFI 页面跳转（Designer 未自动生成，手动添加）
    void gotoScreen_WIFIScreenNoTransition();
    void gotoScreen_WIFIScreenNoTransitionImpl();

    touchgfx::Callback<FrontendApplication> transitionCallbackWIFI;
private:
};

#endif // FRONTENDAPPLICATION_HPP
