#ifndef CIRCLEPROGRESS_HPP
#define CIRCLEPROGRESS_HPP

#include <gui_generated/containers/circleProgressBase.hpp>
#include <touchgfx/Unicode.hpp>

class circleProgress : public circleProgressBase
{
public:
    circleProgress();
    virtual ~circleProgress() {}
    virtual void Data_Refresh();                    // 刷新圆形进度框的数据（从全局变量读取，向后兼容）
    void Data_RefreshParams(uint8_t cur, uint8_t total, uint8_t is_smt);  // 参数化刷新（推荐使用）

    virtual void initialize();
protected:
    static const uint8_t TEXT_PROGRESS_SIZE = 10;
    touchgfx::Unicode::UnicodeChar wildcard1Buffer[TEXT_PROGRESS_SIZE];
    touchgfx::Unicode::UnicodeChar wildcard2Buffer[TEXT_PROGRESS_SIZE];
};

#endif // CIRCLEPROGRESS_HPP