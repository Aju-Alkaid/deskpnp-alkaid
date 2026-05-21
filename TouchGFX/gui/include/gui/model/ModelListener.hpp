#ifndef MODELLISTENER_HPP
#define MODELLISTENER_HPP

#include <gui/model/Model.hpp>

/*
 * ModelListener — Presenter 实现此接口以接收 Model 事件。
 *
 * 命名约定：
 *   onKey*          = 按键事件
 *   onNotify*       = System→GUI 通知（来自 dataTransferQueue）
 *   onCmdResponse*  = GUI→System 命令的响应（未来扩展）
 */
class ModelListener
{
public:
    ModelListener() : model(0) {}
    virtual ~ModelListener() {}

    // ---- 按键事件 ----
    virtual void onKeyPressed(uint8_t key) {}

    // ---- System→GUI 通知（覆写需要的方法即可）----
    virtual void onNotifySMTStatus(uint8_t is_smt)        {}
    virtual void onNotifyTemp(uint16_t temp)               {}
    virtual void onNotifyDownloadStatus(uint8_t status)    {}
    virtual void onNotifySMTProgress(uint8_t cur, uint8_t total) {}
    virtual void onNotifyMotorResetDone()                  {}
    virtual void onNotifyCustom(uint8_t code, uint8_t param) {}

    void bind(Model* m)
    {
        model = m;
    }
protected:
    Model* model;
};

#endif