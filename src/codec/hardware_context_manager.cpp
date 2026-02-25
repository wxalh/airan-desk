#include "hardware_context_manager.h"
#include "../common/logger_manager.h"
#include <QMutex>

HardwareContextManager &HardwareContextManager::instance()
{
    static HardwareContextManager instance;
    return instance;
}

AVBufferRef *HardwareContextManager::getDeviceContext(AVHWDeviceType hwType)
{
    QMutexLocker locker(&m_mutex);

    if (m_failedTypes.contains(hwType))
    {
        const char *hwName = av_hwdevice_get_type_name(hwType);
        LOG_DEBUG("Previously failed to create device context for {}, skipping reattempt", hwName ? hwName : "unknown");
        return nullptr;
    }

    if (m_contexts.contains(hwType))
    {
        AVBufferRef *ctx = m_contexts[hwType];
        if (ctx)
        {
            return av_buffer_ref(ctx);
        }
        else
        {
            m_contexts.remove(hwType);
        }
    }

    AVBufferRef *newCtx = nullptr;
    int ret = av_hwdevice_ctx_create(&newCtx, hwType, nullptr, nullptr, 0);
    if (ret < 0 && hwType == AV_HWDEVICE_TYPE_QSV)
    {
        ret = av_hwdevice_ctx_create(&newCtx, hwType, "auto", nullptr, 0);
    }

    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        const char *hwName = av_hwdevice_get_type_name(hwType);
        LOG_WARN("Failed to create shared hardware device context {}: {} (code={})", hwName ? hwName : "unknown", errbuf, ret);
        // cache failure to avoid thrashing loader (DLL load/unload) on repeated attempts
        m_failedTypes.insert(hwType);
        return nullptr;
    }
    m_contexts[hwType] = newCtx;
    {
        const char *hwName = av_hwdevice_get_type_name(hwType);
        LOG_DEBUG("Created shared hardware device context for: {}", hwName ? hwName : "unknown");
    }
    return av_buffer_ref(newCtx);
}

HardwareContextManager::~HardwareContextManager()
{
    QMutexLocker locker(&m_mutex);
    for (auto it = m_contexts.begin(); it != m_contexts.end(); ++it)
    {
        AVBufferRef *buf = it.value();
        if (buf)
        {
            av_buffer_unref(&buf);
        }
    }
    m_contexts.clear();
    m_failedTypes.clear();
}
