#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QDateTime>
#include <QMetaObject>
#include <QMutexLocker>

namespace
{
constexpr size_t kMaxInputChannelMessageBytes = 1024 * 1024;
}


void WebRtcCli::enqueueInputMoveMessage(const QJsonObject &object)
{
    if (m_shutdownStarted.load())
        return;

    bool scheduleDrain = false;
    {
        QMutexLocker locker(&m_inputMoveIngressMutex);
        if (m_shutdownStarted.load())
            return;
        m_pendingInputMoveMessage = object;
        m_pendingInputMoveBoundary = m_pendingInputMoveBoundary ||
                                     JsonUtil::getBool(object, Constant::KEY_MOVE_BOUNDARY, false);
        if (m_pendingInputMoveBoundary)
            m_pendingInputMoveMessage.insert(Constant::KEY_MOVE_BOUNDARY, true);
        if (!m_inputMoveIngressScheduled)
        {
            m_inputMoveIngressScheduled = true;
            scheduleDrain = true;
        }
    }

    if (scheduleDrain &&
        !QMetaObject::invokeMethod(this, "drainInputMoveMessage", Qt::QueuedConnection))
    {
        QMutexLocker locker(&m_inputMoveIngressMutex);
        m_inputMoveIngressScheduled = false;
    }
}


void WebRtcCli::drainInputMoveMessage()
{
    QJsonObject object;
    {
        QMutexLocker locker(&m_inputMoveIngressMutex);
        object = std::move(m_pendingInputMoveMessage);
        m_pendingInputMoveMessage = QJsonObject();
        m_pendingInputMoveBoundary = false;
        m_inputMoveIngressScheduled = false;
    }

    if (!m_shutdownStarted.load() && !object.isEmpty())
        parseInputMsgIfAlive(object);
}


void WebRtcCli::setupInputChannelCallbacks()
{
    if (!m_inputChannel)
        return;

    m_inputChannel->onOpen(makeWeakCallback(this, &WebRtcCli::onInputChannelOpen, m_callbackLifetime));
    m_inputChannel->onMessage(makeWeakCallback(this, &WebRtcCli::onInputChannelMessage, m_callbackLifetime));
    m_inputChannel->onError(makeWeakCallback(this, &WebRtcCli::onInputChannelError, m_callbackLifetime));
    m_inputChannel->onClosed(makeWeakCallback(this, &WebRtcCli::onInputChannelClosed, m_callbackLifetime));
}


void WebRtcCli::setupInputMoveChannelCallbacks()
{
    if (!m_inputMoveChannel)
        return;

    m_inputMoveChannel->onOpen(makeWeakCallback(this, &WebRtcCli::onInputMoveChannelOpen, m_callbackLifetime));
    m_inputMoveChannel->onMessage(makeWeakCallback(this, &WebRtcCli::onInputChannelMessage, m_callbackLifetime));
    m_inputMoveChannel->onError(makeWeakCallback(this, &WebRtcCli::onInputMoveChannelError, m_callbackLifetime));
    m_inputMoveChannel->onClosed(makeWeakCallback(this, &WebRtcCli::onInputMoveChannelClosed, m_callbackLifetime));
}


void WebRtcCli::onInputChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "onInputChannelOpen", Qt::QueuedConnection);
        return;
    }

    if (m_inputChannelRecoverTimer)
        QMetaObject::invokeMethod(m_inputChannelRecoverTimer, "stop", Qt::QueuedConnection);
    LOG_INFO("Input channel opened");
    flushPendingInputMessages();
    QMetaObject::invokeMethod(this, "notifyCurrentStreamConfig", Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, "notifyDesktopState", Qt::QueuedConnection);
}


void WebRtcCli::onInputChannelMessage(rtc::message_variant data)
{
    if (m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (std::holds_alternative<std::string>(data))
    {
        const std::string &message = std::get<std::string>(data);
        if (message.size() > kMaxInputChannelMessageBytes)
        {
            LOG_WARN("Rejected oversized input channel message: size={} bytes", message.size());
            return;
        }
        const QByteArray messageBytes = QByteArray::fromRawData(message.data(), static_cast<int>(message.size()));

        QJsonParseError parseError{};
        QJsonDocument doc = QJsonDocument::fromJson(messageBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError)
        {
            LOG_ERROR("Input channel message parse error: {}", parseError.errorString());
            return;
        }

        const QJsonObject object = doc.object();
        if (QThread::currentThread() == thread())
        {
            parseInputMsg(object);
        }
        else if (JsonUtil::getString(object, Constant::KEY_MSGTYPE) == Constant::TYPE_MOUSE &&
                 JsonUtil::getString(object, Constant::KEY_DWFLAGS) == Constant::KEY_MOVE)
        {
            enqueueInputMoveMessage(object);
        }
        else
        {
            QMetaObject::invokeMethod(this, "parseInputMsgIfAlive", Qt::QueuedConnection,
                                      Q_ARG(QJsonObject, object));
        }
    }
}


void WebRtcCli::onInputChannelError(std::string error)
{
    if (QThread::currentThread() != thread())
    {
        const QString reason = QString::fromStdString(error);
        QMetaObject::invokeMethod(this, "handleInputChannelErrorOnThread", Qt::QueuedConnection,
                                  Q_ARG(QString, reason));
        return;
    }

    LOG_ERROR("Input channel error: {}", error);
    m_disconnectReason = QStringLiteral("input_channel_error");
    emit destroyCli();
}


void WebRtcCli::onInputChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "onInputChannelClosed", Qt::QueuedConnection);
        return;
    }

    LOG_INFO("Input channel closed");
    if (!m_destroying && !m_isOnlyFile)
    {
        LOG_INFO("Input channel closed by control side, stopping media capture and destroying controlled session");
        m_disconnectReason = QStringLiteral("input_channel_closed");
        emit destroyCli();
    }
}


void WebRtcCli::onInputMoveChannelOpen()
{
    if (!m_shutdownStarted.load())
        LOG_INFO("Low-latency mouse movement channel opened");
}


void WebRtcCli::onInputMoveChannelError(std::string error)
{
    if (!m_shutdownStarted.load())
        LOG_WARN("Low-latency mouse movement channel error: {}; reliable input fallback remains active", error);
}


void WebRtcCli::onInputMoveChannelClosed()
{
    if (!m_shutdownStarted.load())
        LOG_WARN("Low-latency mouse movement channel closed; using reliable input fallback");
}


void WebRtcCli::handleInputChannelErrorOnThread(const QString &reason)
{
    if (m_shutdownStarted.load())
        return;
    onInputChannelError(reason.toStdString());
}


void WebRtcCli::parseInputMsgIfAlive(const QJsonObject &object)
{
    if (m_shutdownStarted.load())
        return;
    if (!m_destroying)
        parseInputMsg(object);
}


void WebRtcCli::scheduleInputChannelRecovery(const QString &reason)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "scheduleInputChannelRecovery", Qt::QueuedConnection,
                                  Q_ARG(QString, reason));
        return;
    }

    if (m_destroying || m_isOnlyFile || !m_peerConnection)
        return;

    if (m_inputChannelRecoverTimer && m_inputChannelRecoverTimer->isActive())
        return;

    LOG_WARN("Input channel unavailable (reason: {}), schedule channel-level renegotiation", reason);
    if (m_inputChannelRecoverTimer)
        m_inputChannelRecoverTimer->start(1200);
}


void WebRtcCli::recoverInputChannel()
{
    if (m_destroying || m_isOnlyFile || !m_peerConnection)
        return;

    
    if (m_inputChannel && m_inputChannel->isOpen())
    {
        LOG_INFO("Input channel already open, skip recovery");
        return;
    }

    try
    {
        if (m_inputChannel)
        {
            try
            {
                m_inputChannel->resetCallbacks();
            }
            catch (...)
            {
            }
            try
            {
                m_inputChannel->close();
            }
            catch (...)
            {
            }
            m_inputChannel.reset();
        }

        if (m_inputMoveChannel)
        {
            try
            {
                m_inputMoveChannel->resetCallbacks();
                m_inputMoveChannel->close();
            }
            catch (...)
            {
            }
            m_inputMoveChannel.reset();
        }

        m_inputChannel = m_peerConnection->createDataChannel(Constant::TYPE_INPUT.toStdString());
        setupInputChannelCallbacks();
        m_inputMoveChannel = m_peerConnection->createDataChannel(
            Constant::TYPE_INPUT_MOVE.toStdString(), {rtc::Reliability{true, 0}});
        setupInputMoveChannelCallbacks();

        
        m_peerConnection->createOffer();
        LOG_INFO("Input channel recreated, renegotiation offer sent");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("recoverInputChannel failed: {}", e.what());
        if (m_inputChannelRecoverTimer)
            m_inputChannelRecoverTimer->start(2000);
    }
    catch (...)
    {
        LOG_ERROR("recoverInputChannel failed: unknown error");
        if (m_inputChannelRecoverTimer)
            m_inputChannelRecoverTimer->start(2000);
    }
}
