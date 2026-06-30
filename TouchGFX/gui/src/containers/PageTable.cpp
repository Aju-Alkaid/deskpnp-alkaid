#include <gui/containers/PageTable.hpp>
#include "key.h"
#include <gui/common/FrontendApplication.hpp>
#include <gui/model/Data_Transfer.h>

PageTable::PageTable()
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

void PageTable::updateDetail()
{
    // TODO: detail1 widget is missing in current UI generator output
}

void PageTable::setWifiConnected(bool connected)
{
    wifi_state = connected;
    applyWifiIcon(page_cnt == 3);
}

void PageTable::applyWifiIcon(bool selected)
{
    wifi_disc.setVisible(!wifi_state && !selected);
    wifi_disc.invalidate();
    wifi_connected.setVisible(wifi_state && !selected);
    wifi_connected.invalidate();
    wifi_disc_selc.setVisible(!wifi_state && selected);
    wifi_disc_selc.invalidate();
    wifi_connected_selc.setVisible(wifi_state && selected);
    wifi_connected_selc.invalidate();
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
            applyWifiIcon(true);
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
            applyWifiIcon(false);
        }
    }
}

void PageTable::handleKey(uint8_t key)
{
    last_page_cnt = page_cnt;
    switch(key){
        case KEY_DOWN:
            page_cnt = (page_cnt + 1) % PAGE_COUNT;
            updateSelection(1, page_cnt);
            updateSelection(0, last_page_cnt);
            break;
        case KEY_UP:
            page_cnt = (page_cnt + PAGE_COUNT - 1) % PAGE_COUNT;
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
                static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_WIFIScreenNoTransition();
                break;
            }
            break;
    }
}
