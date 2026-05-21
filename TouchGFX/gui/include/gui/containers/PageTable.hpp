#ifndef PAGETABLE_HPP
#define PAGETABLE_HPP

#include <gui_generated/containers/PageTableBase.hpp>
#include <touchgfx/widgets/TextArea.hpp>

class PageTable : public PageTableBase
{
public:
    enum RefreState { REFRE_NORMAL, REFRE_RESETING, REFRE_DONE };

    PageTable();
    virtual ~PageTable() {}

    virtual void initialize();
    virtual void blinkHome_selc();
    virtual void blinkLog_selc();
    virtual void blinkImport_selc();
    virtual void blinkRefre_selc();
    virtual void updateDetail();
    virtual void updateSelection(bool flag, uint8_t page);
    virtual void handleKey(uint8_t key);
    virtual void refre_handleKey(uint8_t key);

    // REFRE 屏幕专用
    void setRefreWidgets(touchgfx::TextArea& caution,
                         touchgfx::TextArea& reseting,
                         touchgfx::TextArea& resetDone);
    void refre_tick();
    RefreState getRefreState() const { return refreState; }
    void setRefreState(RefreState s)   { refreState = s; }

protected:
    void motorReset();

    RefreState refreState;

    touchgfx::TextArea* pCaution;
    touchgfx::TextArea* pReseting;
    touchgfx::TextArea* pResetDone;

    uint8_t last_page_cnt;
    uint8_t page_cnt = 0;
};

#endif // PAGETABLE_HPP