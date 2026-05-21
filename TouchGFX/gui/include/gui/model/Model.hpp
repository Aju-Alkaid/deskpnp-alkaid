#ifndef MODEL_HPP
#define MODEL_HPP
#include <touchgfx/hal/Types.hpp>
#include <gui/model/Data_Transfer.h>

class ModelListener;

class Model
{
public:
    Model();

    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    void tick();

    // System→GUI: 处理 dataTransferQueue 中的通知
    void processQueue(void);

    // GUI→System: 发送命令（Presenter 通过此方法向主系统发令）
    void sendCommand(DT_MsgType_t type, int32_t p1 = 0, int32_t p2 = 0);

protected:
    ModelListener* modelListener;
};

#endif