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

    // 处理从主系统发来的数据更新消息
    void processQueue(void);

protected:
    ModelListener* modelListener;

};

#endif // MODEL_HPP