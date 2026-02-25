#ifndef HARDWARE_CONTEXT_MANAGER_H
#define HARDWARE_CONTEXT_MANAGER_H

#include <QMutex>
#include <QMap>
#include <QSet>
#include <QString>
extern "C"
{
#include <libavutil/hwcontext.h>
#include <libavutil/buffer.h>
}

// 硬件设备上下文管理器 - 单例模式，避免重复创建硬件上下文
class HardwareContextManager
{
public:
    static HardwareContextManager &instance();
    AVBufferRef *getDeviceContext(AVHWDeviceType hwType);
    ~HardwareContextManager();
    
private:
    QMap<AVHWDeviceType, AVBufferRef *> m_contexts;
    QMutex m_mutex;
    QSet<AVHWDeviceType> m_failedTypes;
};

#endif // HARDWARE_CONTEXT_MANAGER_H