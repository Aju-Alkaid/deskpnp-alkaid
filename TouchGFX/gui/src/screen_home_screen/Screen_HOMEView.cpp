#include <gui/screen_home_screen/Screen_HOMEView.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <gui/model/Data_Transfer.h>


Screen_HOMEView::Screen_HOMEView()
    : last_if_now_SMT(0xFF)
    , last_total_SMT(0xFF)
    , last_now_SMT(0xFF)
    , last_Temp(0xFF)

{

}

void Screen_HOMEView::setupScreen()
{
    Screen_HOMEViewBase::setupScreen();

    // 绑定温度文本的 wildcard 缓冲区
    temperature.setWildcard(wildcard1Buffer);

    // 初始化完成后立即刷新一次，显示初始状态
    handleFreshEvent();

    // 更新缓存
    last_if_now_SMT = if_now_SMT;
    last_total_SMT = total_SMT;
    last_now_SMT   = now_SMT;
    last_Temp      = Temp;
}

void Screen_HOMEView::tearDownScreen()
{
    Screen_HOMEViewBase::tearDownScreen();
}

void Screen_HOMEView::handleTickEvent()
{
    // 仅在 Data_Transfer 数据变化时才刷新屏幕
    if (if_now_SMT != last_if_now_SMT ||
        total_SMT  != last_total_SMT  ||
        now_SMT    != last_now_SMT    ||
        Temp       != last_Temp)
    {
        handleFreshEvent();

        last_if_now_SMT = if_now_SMT;
        last_total_SMT  = total_SMT;
        last_now_SMT    = now_SMT;
        last_Temp       = Temp;
    }
}

void Screen_HOMEView::handleKeyEvent(uint8_t key)
{
    // 将按键事件传递给 PageTable 容器
    pageTable.handleKey(key);
}

void Screen_HOMEView::handleFreshEvent()
{
    // 刷新贴片进度
    Progress1.Data_Refresh();

    // 刷新温度数据
    touchgfx::Unicode::snprintf(wildcard1Buffer, 10, "%u", Temp);
    temperature.invalidate();
}