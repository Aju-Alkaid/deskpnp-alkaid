#ifndef CIRCLEPROGRESS_HPP
#define CIRCLEPROGRESS_HPP

#include <gui_generated/containers/circleProgressBase.hpp>
#include <touchgfx/Unicode.hpp>

class circleProgress : public circleProgressBase
{
public:
    circleProgress();
    virtual ~circleProgress() {}
    virtual void Data_Refresh();    // 刷新圆形进度框的数据

    virtual void initialize();
protected:
    static const uint8_t TEXT_PROGRESS_SIZE = 10;
    touchgfx::Unicode::UnicodeChar wildcard1Buffer[TEXT_PROGRESS_SIZE];
    touchgfx::Unicode::UnicodeChar wildcard2Buffer[TEXT_PROGRESS_SIZE];
};

#endif // CIRCLEPROGRESS_HPP