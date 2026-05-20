#include <gui/containers/PageTable.hpp>
#include "key.h"
#include <gui/common/FrontendApplication.hpp>
#include <gui/model/Data_Transfer.h>

PageTable::PageTable()
    : refreState(REFRE_NORMAL)
    , pCaution(0)
    , pReseting(0)
    , pResetDone(0)
{
}

void PageTable::initialize()
{
    PageTableBase::initialize();
}

void PageTable::blinkHome_selc()
{
    home_selc.setVisible(!home_selc.isVisible());
    home_selc.invalidate();
}

void PageTable::blinkLog_selc()
{
    log_selc.setVisible(!log_selc.isVisible());
    log_selc.invalidate();
}

void PageTable::blinkImport_selc()
{
    import_selc.setVisible(!import_selc.isVisible());
    import_selc.invalidate();
}

void PageTable::blinkRefre_selc()
{
    refre_selc.setVisible(!refre_selc.isVisible());
    refre_selc.invalidate();
}

void PageTable::updateDetail()
{
    detail1.setVisible(!detail1.isVisible());
    detail1.invalidate();
}

void PageTable::updateSelection(bool flag, uint8_t page)
{
    if(flag == 1){
        if(page == 0){
            home_selc.setVisible(true);
            home_selc.invalidate();
        }
        if(page == 1){
            import_selc.setVisible(true);
            import_selc.invalidate();
        }
        if(page == 2){
            log_selc.setVisible(true);
            log_selc.invalidate();
        }
        if(page == 3){
            refre_selc.setVisible(true);
            refre_selc.invalidate();
        }
    }
    if(flag == 0){
        if(page == 0){
            home_selc.setVisible(false);
            home_selc.invalidate();
        }
        if(page == 1){
            import_selc.setVisible(false);
            import_selc.invalidate();
        }
        if(page == 2){
            log_selc.setVisible(false);
            log_selc.invalidate();
        }
        if(page == 3){
            refre_selc.setVisible(false);
            refre_selc.invalidate();
        }
    }
}

void PageTable::handleKey(uint8_t key)
{
    last_page_cnt = page_cnt;
		switch(key){
			case KEY_DOWN:
        page_cnt = (page_cnt + 1) % 4;
        updateSelection(1, page_cnt);
        updateSelection(0, last_page_cnt);
				break;
      case KEY_UP:
        page_cnt = (page_cnt + 3) % 4;
        updateSelection(1, page_cnt);
        updateSelection(0, last_page_cnt);
				break;
			case KEY_KEY1:
        updateDetail();
				break;
			case KEY_KEY2:
        switch (page_cnt) {
        case 0:
            static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_HOMEScreenNoTransition();
            break;
        case 1:
            static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_IMPORTScreenNoTransition();
            break;
        case 2:
            static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_LOGScreenNoTransition();
            break;
        case 3:
            static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_RESETScreenNoTransition();
            break;
        }
				break;
    }
}

void PageTable::setRefreWidgets(touchgfx::TextArea& caution,
                                 touchgfx::TextArea& reseting,
                                 touchgfx::TextArea& resetDone)
{
    pCaution   = &caution;
    pReseting  = &reseting;
    pResetDone = &resetDone;
}

void PageTable::refre_handleKey(uint8_t key)
{
    if (key == KEY_DOWN) {
        static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_HOMEScreenNoTransition();

    } else if (key == KEY_UP) {
        if (!pCaution || !pReseting) return;

        pCaution->setVisible(false);
        pCaution->invalidate();

        pReseting->setVisible(true);
        pReseting->invalidate();

        motorReset_Start();
        refreState = REFRE_RESETING;

    } else if (key == KEY_KEY1) {
        updateDetail();
    }
}

void PageTable::refre_tick()
{
    switch (refreState) {
    case REFRE_RESETING:
        if (motorReset_IsDone()) {
            if (pReseting) {
                pReseting->setVisible(false);
                pReseting->invalidate();
            }
            if (pResetDone) {
                pResetDone->setVisible(true);
                pResetDone->invalidate();
            }
            refreState = REFRE_DONE;
        }
        break;

    case REFRE_DONE:
        break;

    case REFRE_NORMAL:
    default:
        break;
    }
}

void PageTable::motorReset()
{
    motorReset_Start();
}