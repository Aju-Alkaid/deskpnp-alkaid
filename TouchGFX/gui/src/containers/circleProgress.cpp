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
    // 更新进度文本: now_SMT / total_SMT
    touchgfx::Unicode::snprintf(wildcard1Buffer, TEXT_PROGRESS_SIZE, "%u", now_SMT);
    touchgfx::Unicode::snprintf(wildcard2Buffer, TEXT_PROGRESS_SIZE, "%u", total_SMT);
    textProgress1.invalidate();

    if (if_now_SMT == 1) {
        Working.setVisible(true);
        Working.invalidate();
        Waiting.setVisible(false);
        Waiting.invalidate();
        circleProgress1.setRange(0, total_SMT > 0 ? total_SMT : 1);
        circleProgress1.setValue(now_SMT);
    } else {
        Working.setVisible(false);
        Working.invalidate();
        Waiting.setVisible(true);
        Waiting.invalidate();
    }
}