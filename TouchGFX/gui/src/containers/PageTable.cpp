#include <gui/containers/PageTable.hpp>
#include "key.h"
#include <gui/common/FrontendApplication.hpp>

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

/**
 * @brief       更新image_selc的显示状态
 * @param flag  bool类型，1为显示，0为隐藏
 * @param page  对应的page，按照touchgfx中的顺序，从0开始数，从上到下
 */
void PageTable::updateSelection(bool flag, uint8_t page)
{
    if(flag == 1){      //show selected icon
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
    if(flag == 0){      //hide selected icon
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

/**
 * @brief      PageTable容器的处理按键事件函数
 * @param key  按下的按键，只处理key1，key2，cw和ccw
 */
void PageTable::handleKey(uint8_t key)      //处理按键事件，按下按键时更新page_cnt，并更新图片
{
    last_page_cnt = page_cnt;

    if (key == down) {
        page_cnt = (page_cnt + 1) % 4;
        updateSelection(1, page_cnt);
        updateSelection(0, last_page_cnt);
    } else if (key == up) {
        page_cnt = (page_cnt + 3) % 4;
        updateSelection(1, page_cnt);
        updateSelection(0, last_page_cnt);
    } else if (key == key1) {       //按下key1展开或关闭detail控件
        updateDetail();
    } else if (key == key2) {       //按下key2切换到指定page
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
            static_cast<FrontendApplication*>(touchgfx::Application::getInstance())->gotoScreen_REFREScreenNoTransition();
            break;
        }
    }
}