#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Utils.hpp>
#include <touchgfx/transitions/NoTransition.hpp>
#include <gui/screen_import_screen/Screen_IMPORTView.hpp>
#include <gui/screen_import_screen/Screen_IMPORTPresenter.hpp>
#include <gui/screen_log_screen/Screen_LOGView.hpp>
#include <gui/screen_log_screen/Screen_LOGPresenter.hpp>
#include <gui/screen_wifi_screen/Screen_WIFIView.hpp>
#include <gui/screen_wifi_screen/Screen_WIFIPresenter.hpp>
#include <gui/common/FrontendHeap.hpp>

using namespace touchgfx;

FrontendApplication::FrontendApplication(Model& m, FrontendHeap& heap)
    : FrontendApplicationBase(m, heap)
{

}

void FrontendApplication::gotoScreen_IMPORTScreenNoTransition()
{
    transitionCallbackIMPORT = touchgfx::Callback<FrontendApplication>(this, &FrontendApplication::gotoScreen_IMPORTScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &transitionCallbackIMPORT;
}

void FrontendApplication::gotoScreen_IMPORTScreenNoTransitionImpl()
{
    touchgfx::makeTransition<Screen_IMPORTView, Screen_IMPORTPresenter, touchgfx::NoTransition, Model >(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoScreen_LOGScreenNoTransition()
{
    transitionCallbackLOG = touchgfx::Callback<FrontendApplication>(this, &FrontendApplication::gotoScreen_LOGScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &transitionCallbackLOG;
}

void FrontendApplication::gotoScreen_LOGScreenNoTransitionImpl()
{
    touchgfx::makeTransition<Screen_LOGView, Screen_LOGPresenter, touchgfx::NoTransition, Model >(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoScreen_WIFIScreenNoTransition()
{
    transitionCallbackWIFI = touchgfx::Callback<FrontendApplication>(this, &FrontendApplication::gotoScreen_WIFIScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &transitionCallbackWIFI;
}

void FrontendApplication::gotoScreen_WIFIScreenNoTransitionImpl()
{
    touchgfx::makeTransition<Screen_WIFIView, Screen_WIFIPresenter, touchgfx::NoTransition, Model >(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}
