#include "webrtc/ctl/webrtc_ctl.h"

#include <QDateTime>

#include <optional>

namespace
{
constexpr size_t kMouseMoveBacklogLimit = 16 * 1024;

struct MouseInputInfo
{
    bool isMove = false;
};

std::optional<MouseInputInfo> parseMouseInputInfo(const rtc::message_variant &data)
{
    if (!std::holds_alternative<std::string>(data))
        return std::nullopt;

    const std::string &message = std::get<std::string>(data);
    const QByteArray bytes(message.data(), static_cast<int>(message.size()));
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;

    const QJsonObject object = document.object();
    if (JsonUtil::getString(object, Constant::KEY_MSGTYPE) != Constant::TYPE_MOUSE)
        return std::nullopt;

    MouseInputInfo info;
    info.isMove = JsonUtil::getString(object, Constant::KEY_DWFLAGS) == Constant::KEY_MOVE;
    return info;
}

rtc::message_variant addMoveMetadata(const rtc::message_variant &data,
                                     const MouseMoveBurstPolicy::Dispatch &dispatch)
{
    if (!std::holds_alternative<std::string>(data))
        return data;

    const std::string &message = std::get<std::string>(data);
    QJsonParseError error{};
    QJsonDocument document = QJsonDocument::fromJson(
        QByteArray(message.data(), static_cast<int>(message.size())), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return data;

    QJsonObject object = document.object();
    object.insert(Constant::KEY_SEQUENCE, static_cast<double>(dispatch.sequence));
    object.insert(Constant::KEY_MOVE_BOUNDARY, dispatch.reliableBoundary);
    return rtc::message_variant(QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString());
}
} // namespace

bool WebRtcCtl::sendInputChannelNow(const rtc::message_variant &data,
                                    bool isMouseMove,
                                    bool reliableBoundary)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return false;

    std::shared_ptr<rtc::DataChannel> channel = m_inputChannel;
    if (isMouseMove && !reliableBoundary && m_inputMoveChannel && m_inputMoveChannel->isOpen())
        channel = m_inputMoveChannel;
    if (!channel || !channel->isOpen())
        return false;

    try
    {
        if (isMouseMove && !reliableBoundary && channel->bufferedAmount() > kMouseMoveBacklogLimit)
        {
            LOG_TRACE("Drop intermediate mouse move due to channel backlog: {} bytes", channel->bufferedAmount());
            return true;
        }

        if (!channel->send(data))
        {
            LOG_TRACE("Input message was not accepted by the data channel");
            return false;
        }
        noteSessionOutboundActivity();
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to send input channel message: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to send input channel message: unknown error");
    }
    return false;
}


bool WebRtcCtl::sendInputMoveDispatch(const rtc::message_variant &data,
                                      const MouseMoveBurstPolicy::Dispatch &dispatch)
{
    const bool sent = sendInputChannelNow(addMoveMetadata(data, dispatch), true, dispatch.reliableBoundary);
    if (!sent && dispatch.reliableBoundary)
    {
        m_mouseMovePolicy.markBoundaryFailed(dispatch.sequence);
        m_pendingInputMoveBoundary = true;
    }
    else if (sent && dispatch.reliableBoundary)
    {
        m_pendingInputMoveBoundary = false;
    }
    return sent;
}


void WebRtcCtl::flushPendingInputMove()
{
    if (!m_hasLatestInputMove)
        return;

    const auto dispatch = m_mouseMovePolicy.takeThrottledMove(QDateTime::currentMSecsSinceEpoch());
    if (dispatch)
        sendInputMoveDispatch(m_latestInputMove, *dispatch);
}


void WebRtcCtl::finishInputMoveBurst()
{
    if (m_inputMoveFlushTimer)
        m_inputMoveFlushTimer->stop();

    const auto dispatch = m_mouseMovePolicy.finishBurst(QDateTime::currentMSecsSinceEpoch());
    bool sent = true;
    if (dispatch && m_hasLatestInputMove)
        sent = sendInputMoveDispatch(m_latestInputMove, *dispatch);
    if (sent && !m_pendingInputMoveBoundary)
        m_hasLatestInputMove = false;
}


void WebRtcCtl::suspendInputMoveBurst()
{
    if (m_inputMoveFlushTimer)
        m_inputMoveFlushTimer->stop();
    if (m_inputMoveTailTimer)
        m_inputMoveTailTimer->stop();
    if (m_hasLatestInputMove)
        m_pendingInputMoveBoundary = true;
}


void WebRtcCtl::retryPendingInputMoveBoundary()
{
    if (!m_pendingInputMoveBoundary || !m_hasLatestInputMove)
        return;
    const auto dispatch = m_mouseMovePolicy.forceLatestBoundary();
    if (dispatch && sendInputMoveDispatch(m_latestInputMove, *dispatch))
        m_hasLatestInputMove = false;
}


void WebRtcCtl::resetInputMoveBurst()
{
    if (m_inputMoveFlushTimer)
        m_inputMoveFlushTimer->stop();
    if (m_inputMoveTailTimer)
        m_inputMoveTailTimer->stop();
    m_mouseMovePolicy.reset();
    m_hasLatestInputMove = false;
    m_pendingInputMoveBoundary = false;
}


void WebRtcCtl::inputChannelSendMsg(const rtc::message_variant &data)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    LOG_TRACE("inputChannelSendMsg called - connected: {}, inputChannel: {}, isOpen: {}",
              m_connected,
              (m_inputChannel != nullptr),
              (m_inputChannel && m_inputChannel->isOpen()));

    if (m_inputChannel && m_inputChannel->isOpen())
    {
        retryPendingInputMoveBoundary();
        const auto mouseInfo = parseMouseInputInfo(data);
        const bool isMove = mouseInfo.has_value() && mouseInfo->isMove;
        if (isMove)
        {
            m_latestInputMove = data;
            m_hasLatestInputMove = true;
            const auto dispatch = m_mouseMovePolicy.observeMove(QDateTime::currentMSecsSinceEpoch());
            if (dispatch.dispatchNow)
            {
                if (m_inputMoveFlushTimer)
                    m_inputMoveFlushTimer->stop();
                sendInputMoveDispatch(m_latestInputMove, dispatch);
            }
            else if (m_inputMoveFlushTimer && !m_inputMoveFlushTimer->isActive())
            {
                m_inputMoveFlushTimer->start(dispatch.throttleDelayMs);
            }

            if (m_inputMoveTailTimer)
                m_inputMoveTailTimer->start(dispatch.tailDelayMs);
            return;
        }

        finishInputMoveBurst();
        if (!sendInputControlMessage(data))
            LOG_TRACE("Reliable input message queued because the input channel rejected it");
    }
    else
    {
        const auto mouseInfo = parseMouseInputInfo(data);
        if (mouseInfo.has_value() && mouseInfo->isMove)
        {
            m_latestInputMove = data;
            m_hasLatestInputMove = true;
            const auto dispatch = m_mouseMovePolicy.observeMove(QDateTime::currentMSecsSinceEpoch());
            if (dispatch.reliableBoundary)
                m_mouseMovePolicy.markBoundaryFailed(dispatch.sequence);
        }
        suspendInputMoveBurst();
        if (!mouseInfo.has_value() || !mouseInfo->isMove)
            queueInputControlMessage(data);
        scheduleReconnect();

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - m_lastInputNotReadyLogMs >= 2000)
        {
            m_lastInputNotReadyLogMs = nowMs;
            LOG_WARN("Input channel not ready for sending - channel exists: {}, channel open: {}",
                     (m_inputChannel != nullptr),
                     (m_inputChannel && m_inputChannel->isOpen()));
        }
    }
}
