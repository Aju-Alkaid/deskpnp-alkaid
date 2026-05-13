#ifndef PAGETABLE_HPP
#define PAGETABLE_HPP

#include <gui_generated/containers/PageTableBase.hpp>

class PageTable : public PageTableBase
{
public:
    PageTable();
    virtual ~PageTable() {}

    virtual void initialize();
    virtual void blinkHome_selc();      //闪烁第一个图标
    virtual void blinkLog_selc();       
    virtual void blinkImport_selc();
    virtual void blinkRefre_selc();     //闪烁第四个图标
    virtual void updateDetail();        //闪烁细节框（容器）
    virtual void updateSelection(bool flag, uint8_t page);
    virtual void handleKey(uint8_t key);
protected:

    uint8_t last_page_cnt;
    uint8_t page_cnt = 0;
};

#endif // PAGETABLE_HPP
