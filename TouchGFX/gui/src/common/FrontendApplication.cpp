#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Utils.hpp>
#include <touchgfx/transitions/NoTransition.hpp>
#include <gui/screen_wifi_screen/Screen_WIFIView.hpp>
#include <gui/screen_wifi_screen/Screen_WIFIPresenter.hpp>
#include <gui/common/FrontendHeap.hpp>

using namespace touchgfx;

FrontendApplication::FrontendApplication(Model& m, FrontendHeap& heap)
    : FrontendApplicationBase(m, heap)
{

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
