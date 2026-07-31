#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QUuid>


void WebRtcCtl::onInputChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onInputChannelOpen();
        });
        return;
    }
    const QString channelLabel = m_inputChannel ? QString::fromStdString(m_inputChannel->label()) : QString();
    LOG_INFO("Input channel opened: {}", channelLabel);
    retryPendingInputMoveBoundary();
    sendStreamConfig();
    requestCurrentAudioMode();
    if (!m_isOnlyFile && m_controlHeartbeatTimer)
    {
        QMetaObject::invokeMethod(m_controlHeartbeatTimer, "start",
                                  Qt::QueuedConnection, Q_ARG(int, 2000));
        QMetaObject::invokeMethod(this, "sendControlHeartbeat", Qt::QueuedConnection);
    }
}


void WebRtcCtl::onInputMoveChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onInputMoveChannelOpen();
        });
        return;
    }
    LOG_INFO("Low-latency mouse movement channel opened");
}


void WebRtcCtl::sendStreamConfig()
{
    if (m_isOnlyFile || !m_inputChannel || !m_inputChannel->isOpen())
        return;

    const int requestedFps = ConfigUtil->fps;

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_STREAM_CONFIG)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, m_remoteId)
                          .add(Constant::KEY_RECEIVER_PWD, m_remotePwdMd5)
                          .add(Constant::KEY_NETWORK_PATH, m_networkPath)
                          .add(Constant::KEY_MEDIA_TOPOLOGY, m_mediaTopology)
                          .add(Constant::KEY_QUALITY_PROFILE, m_qualityProfile)
                          .add(Constant::KEY_WIDTH, m_requestedWidth)
                          .add(Constant::KEY_HEIGHT, m_requestedHeight)
                          .add(Constant::KEY_FPS, requestedFps)
                          .add(Constant::KEY_ENABLE_WGC_CAPTURE, ConfigUtil->enable_wgc_capture)
                          .add(Constant::KEY_ENABLE_DXGI_CAPTURE, ConfigUtil->enable_dxgi_capture)
                          .add(Constant::KEY_ENABLE_DXGI_NATIVE_GPU_CAPTURE, ConfigUtil->enable_dxgi_native_gpu_capture)
                          .build();

    try
    {
        m_inputChannel->send(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
        noteSessionOutboundActivity();
        LOG_INFO("Initial stream config sent: networkPath={}, mediaTopology={}, qualityProfile={}, resolution={}x{}, maxFps={}, wgc={}, dxgi={}, dxgiNativeGpu={}",
                 m_networkPath,
                 m_mediaTopology,
                 m_qualityProfile,
                 m_requestedWidth,
                 m_requestedHeight,
                 requestedFps,
                 ConfigUtil->enable_wgc_capture,
                 ConfigUtil->enable_dxgi_capture,
                 ConfigUtil->enable_dxgi_native_gpu_capture);
    }
    catch (const std::exception &e)
    {
        LOG_WARN("Failed to send initial stream config: {}", e.what());
    }
}

void WebRtcCtl::requestCurrentAudioMode()
{
    if (m_isOnlyFile || !m_inputChannel || !m_inputChannel->isOpen() || m_audioMode == QStringLiteral("off"))
        return;

    QString requestId = QUuid::createUuid().toString();
    requestId.remove(QLatin1Char('{'));
    requestId.remove(QLatin1Char('}'));

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_AUDIO_CAPTURE)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, m_remoteId)
                          .add(Constant::KEY_RECEIVER_PWD, m_remotePwdMd5)
                          .add(Constant::KEY_REQUEST_ID, requestId)
                          .add(Constant::KEY_ENABLED, true)
                          .add(Constant::KEY_AUDIO_MODE, m_audioMode)
                          .build();

    try
    {
        m_inputChannel->send(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
        noteSessionOutboundActivity();
        LOG_INFO("Initial remote audio mode confirmation requested: mode={}, requestId={}", m_audioMode, requestId);
    }
    catch (const std::exception &e)
    {
        LOG_WARN("Failed to request initial remote audio mode confirmation: {}", e.what());
    }
}


void WebRtcCtl::onInputChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onInputChannelClosed();
        });
        return;
    }
    const QString channelLabel = m_inputChannel ? QString::fromStdString(m_inputChannel->label()) : QString();
    LOG_INFO("Input channel closed: {}", channelLabel);
    suspendInputMoveBurst();
    if (m_controlHeartbeatTimer)
        QMetaObject::invokeMethod(m_controlHeartbeatTimer, "stop", Qt::QueuedConnection);
    scheduleReconnect();
}


void WebRtcCtl::onInputMoveChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    LOG_WARN("Low-latency mouse movement channel closed; using reliable input fallback");
}


void WebRtcCtl::onInputMoveChannelError(const std::string &error)
{
    if (m_shutdownStarted.load())
        return;
    LOG_WARN("Low-latency mouse movement channel error: {}; using reliable input fallback", error);
}


void WebRtcCtl::onInputChannelError(const std::string &error)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const std::string errorCopy = error;
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, errorCopy]() {
            if (guard)
                guard->onInputChannelError(errorCopy);
        });
        return;
    }
    LOG_ERROR("Input channel error: {}", error);
    if (m_controlHeartbeatTimer)
        QMetaObject::invokeMethod(m_controlHeartbeatTimer, "stop", Qt::QueuedConnection);
    scheduleReconnect();
}


void WebRtcCtl::onInputChannelMessage(const rtc::message_variant &message)
{
    if (m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (!std::holds_alternative<std::string>(message))
    {
        LOG_DEBUG("Input channel binary message received (control side), ignoring");
        return;
    }
    constexpr size_t kMaxInputChannelMessageBytes = 1024 * 1024;
    const std::string &text = std::get<std::string>(message);
    if (text.size() > kMaxInputChannelMessageBytes)
    {
        LOG_WARN("Rejected oversized input channel message: size={} bytes", text.size());
        return;
    }
    if (QThread::currentThread() != thread())
    {
        const rtc::message_variant messageCopy = message;
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, messageCopy]() {
            if (guard)
                guard->onInputChannelMessage(messageCopy);
        });
        return;
    }
    {
        QJsonObject object = JsonUtil::safeParseObject(
            QByteArray::fromRawData(text.data(), static_cast<int>(text.size())));
        const QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
        if (msgType == Constant::TYPE_STREAM_CONFIG)
        {
            applyLocalStreamConfig(object);
        }
        else if (msgType == Constant::TYPE_DESKTOP_STATE)
        {
            const bool locked = JsonUtil::getBool(object, Constant::KEY_LOCKED, false);
            const QString messageText = JsonUtil::getString(object, Constant::KEY_MESSAGE);
            m_remoteDesktopLocked = locked;
            Q_EMIT desktopStateChanged(locked, messageText);
            LOG_INFO("Desktop state updated: locked={}, message={}", locked, messageText);
        }
        else if (msgType == Constant::TYPE_AUDIO_CAPTURE)
        {
            const bool statusOnly = JsonUtil::getBool(object, Constant::KEY_STATUS_ONLY, false);
            if (statusOnly)
            {
                const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
                const QString mode = JsonUtil::getString(object, Constant::KEY_AUDIO_MODE);
                const bool accepted = JsonUtil::getBool(object, Constant::KEY_ACCEPTED, false);
                const QString messageText = JsonUtil::getString(object, Constant::KEY_MESSAGE);
                Q_EMIT audioModeRequestFinished(requestId, mode, accepted, messageText);
                LOG_INFO("Audio mode response received: mode={}, accepted={}, requestId={}", mode, accepted, requestId);
            }
        }
        else
        {
            LOG_DEBUG("Input channel message received (control side)");
        }
    }
}
