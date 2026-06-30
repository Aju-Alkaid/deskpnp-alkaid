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

    // Designer 未自动生成的页面跳转，手动添加
    void gotoScreen_IMPORTScreenNoTransition();
    void gotoScreen_LOGScreenNoTransition();
    void gotoScreen_WIFIScreenNoTransition();

protected:
    void gotoScreen_IMPORTScreenNoTransitionImpl();
    void gotoScreen_LOGScreenNoTransitionImpl();
    void gotoScreen_WIFIScreenNoTransitionImpl();

    touchgfx::Callback<FrontendApplication> transitionCallbackIMPORT;
    touchgfx::Callback<FrontendApplication> transitionCallbackLOG;
    touchgfx::Callback<FrontendApplication> transitionCallbackWIFI;
private:
};

#endif // FRONTENDAPPLICATION_HPP
