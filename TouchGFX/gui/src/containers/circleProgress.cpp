#include <gui/containers/circleProgress.hpp>
#include <gui/model/Data_Transfer.h>

circleProgress::circleProgress()
{

}

void circleProgress::initialize()
{
    circleProgressBase::initialize();

    // 绑定 wildcard 缓冲区
    textProgress1.setWildcard1(wildcard1Buffer);
    textProgress1.setWildcard2(wildcard2Buffer);

    // 设置 Working / Waiting 的显式尺寸（1bpp 下 auto-size 可能不准）
    Working.setWidth(120);
    Working.setHeight(35);
    Waiting.setWidth(120);
    Waiting.setHeight(35);
}

void circleProgress::Data_Refresh()
{
    /* 向后兼容：从全局变量读取。新代码应优先使用 Data_RefreshParams() */
    Data_RefreshParams(now_SMT, total_SMT, if_now_SMT);
}

void circleProgress::Data_RefreshParams(uint8_t cur, uint8_t total, uint8_t is_smt)
{
    touchgfx::Unicode::snprintf(wildcard1Buffer, TEXT_PROGRESS_SIZE, "%u", cur);
    touchgfx::Unicode::snprintf(wildcard2Buffer, TEXT_PROGRESS_SIZE, "%u", total);
    textProgress1.invalidate();

    if (is_smt == 1) {
        Working.setVisible(true);
        Working.invalidate();
        Waiting.setVisible(false);
        Waiting.invalidate();
        circleProgress1.setRange(0, total > 0 ? total : 1);
        circleProgress1.setValue(cur);
    } else {
        Working.setVisible(false);
        Working.invalidate();
        Waiting.setVisible(true);
        Waiting.invalidate();
    }
}