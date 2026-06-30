#ifndef PAGETABLE_HPP
#define PAGETABLE_HPP

#include <gui_generated/containers/PageTableBase.hpp>
#include <touchgfx/widgets/TextArea.hpp>

class PageTable : public PageTableBase
{
public:
    PageTable();
    virtual ~PageTable() {}

    virtual void initialize();
    virtual void blinkHome_selc();
    virtual void blinkLog_selc();
    virtual void blinkImport_selc();
    virtual void updateDetail();
    virtual void updateSelection(bool flag, uint8_t page);
    virtual void handleKey(uint8_t key);
    void setWifiConnected(bool connected);

protected:
    static const uint8_t PAGE_COUNT = 4;
    uint8_t last_page_cnt;
    uint8_t page_cnt = 0;
    bool wifi_state = false;
    void applyWifiIcon(bool selected);
};

#endif // PAGETABLE_HPP
