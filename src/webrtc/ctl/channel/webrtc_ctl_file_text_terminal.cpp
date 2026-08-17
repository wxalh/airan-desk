#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"
#include "util/json/json_util.h"

#include <QTimer>

namespace
{
constexpr int kTerminalOutputFlushIntervalMs = 16;
constexpr int kTerminalOutputBatchBytes = 64 * 1024;
constexpr qint64 kTerminalOutputHighWatermark = 512LL * 1024;
constexpr qint64 kTerminalOutputLowWatermark = 128 * 1024;
constexpr int kMaxTerminalOutputMessageEncodedBytes = 1024 * 1024;
constexpr int kMaxPendingTerminalOutputBytes = 4 * 1024 * 1024;
const QByteArray kTerminalOutputResync = QByteArrayLiteral("\x18\x1a\x1b[2J\x1b[H\x1b[0m");
}


bool WebRtcCtl::isCurrentTerminalResponse(const QString &requestId) const
{
    return requestId.isEmpty() || requestId == m_terminalStartRequestId;
}


void WebRtcCtl::beginTerminalStartGeneration(const QString &requestId)
{
    const bool replacingGeneration = !m_terminalStartRequestId.isEmpty() &&
                                     requestId != m_terminalStartRequestId;
    m_terminalStartRequestId = requestId;
    if (!replacingGeneration)
        return;

    if (m_terminalOutputFlushTimer)
        m_terminalOutputFlushTimer->stop();
    m_pendingTerminalOutput.clear();
    m_terminalOutputInFlight = 0;
    m_terminalConsumerBacklog = 0;
    m_terminalFlowPaused = false;
    m_terminalOutputNeedsReset = true;
}


bool WebRtcCtl::handleTerminalTextChannelObject(const QJsonObject &object, const QString &msgType)
{
    if (msgType == Constant::TYPE_TERMINAL_OUTPUT)
    {
        const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
        if (!isCurrentTerminalResponse(requestId))
        {
            LOG_DEBUG("Ignoring stale terminal output: requestId={}, expected={}", requestId, m_terminalStartRequestId);
            return true;
        }
        const QString encodedData = JsonUtil::getString(object, Constant::KEY_DATA);
        if (encodedData.size() > kMaxTerminalOutputMessageEncodedBytes)
        {
            LOG_WARN("Rejected oversized terminal output message: encodedSize={} bytes", encodedData.size());
            sendTerminalFlowControl(false);
            return true;
        }
        const QByteArray bytes = QByteArray::fromBase64(encodedData.toLatin1());
        const int resetBytes = m_terminalOutputNeedsReset ? kTerminalOutputResync.size() : 0;
        if (bytes.size() + resetBytes > kMaxPendingTerminalOutputBytes - m_pendingTerminalOutput.size())
        {
            LOG_WARN("Dropped terminal output because the pending buffer is full: size={}, pending={}",
                     bytes.size(), m_pendingTerminalOutput.size());
            m_terminalOutputNeedsReset = true;
            sendTerminalFlowControl(false);
            return true;
        }
        if (m_terminalOutputNeedsReset)
        {
            m_pendingTerminalOutput.append(kTerminalOutputResync);
            m_terminalOutputNeedsReset = false;
        }
        m_pendingTerminalOutput.append(bytes);
        if (!m_terminalOutputFlushTimer)
        {
            m_terminalOutputFlushTimer = new QTimer(this);
            m_terminalOutputFlushTimer->setSingleShot(true);
            m_terminalOutputFlushTimer->setInterval(kTerminalOutputFlushIntervalMs);
            connect(m_terminalOutputFlushTimer, &QTimer::timeout,
                    this, &WebRtcCtl::flushTerminalOutput);
        }
        if (!m_terminalOutputFlushTimer->isActive())
            m_terminalOutputFlushTimer->start();
        updateTerminalFlowControl();
        return true;
    }
    if (msgType == Constant::TYPE_TERMINAL_INFO)
    {
        emit terminalInfo(JsonUtil::getString(object, Constant::KEY_OS),
                          JsonUtil::getString(object, Constant::KEY_SHELL),
                          JsonUtil::getString(object, Constant::KEY_TERMINAL_MODE),
                          JsonUtil::getBool(object, Constant::KEY_PATH_TRACKING, true),
                          JsonUtil::getBool(object, Constant::KEY_PATH_TRACKING_READY, false),
                          JsonUtil::getString(object, Constant::KEY_REQUEST_ID));
        return true;
    }
    if (msgType == Constant::TYPE_TERMINAL_CLOSED)
    {
        const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
        if (!isCurrentTerminalResponse(requestId))
        {
            LOG_DEBUG("Ignoring stale terminal closed event: requestId={}, expected={}", requestId, m_terminalStartRequestId);
            return true;
        }
        flushTerminalOutputBeforeSessionEnd();
        m_pendingTerminalOutput.clear();
        m_terminalOutputInFlight = 0;
        m_terminalConsumerBacklog = 0;
        const bool wasFlowPaused = m_terminalFlowPaused;
        m_terminalFlowPaused = false;
        m_terminalOutputNeedsReset = false;
        if (wasFlowPaused)
            sendTerminalFlowControl(true);
        emit terminalClosed(JsonUtil::getInt(object, Constant::KEY_STATUS, 0), requestId);
        return true;
    }
    if (msgType == Constant::TYPE_TERMINAL_ERROR)
    {
        const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
        if (!isCurrentTerminalResponse(requestId))
        {
            LOG_DEBUG("Ignoring stale terminal error event: requestId={}, expected={}", requestId, m_terminalStartRequestId);
            return true;
        }
        flushTerminalOutputBeforeSessionEnd();
        m_pendingTerminalOutput.clear();
        m_terminalOutputInFlight = 0;
        m_terminalConsumerBacklog = 0;
        const bool wasFlowPaused = m_terminalFlowPaused;
        m_terminalFlowPaused = false;
        m_terminalOutputNeedsReset = false;
        if (wasFlowPaused)
            sendTerminalFlowControl(true);
        emit terminalError(JsonUtil::getString(object, Constant::KEY_ERROR), requestId);
        return true;
    }
    return false;
}


void WebRtcCtl::flushTerminalOutputBeforeSessionEnd()
{
    if (m_terminalOutputFlushTimer)
        m_terminalOutputFlushTimer->stop();
    if (m_terminalOutputNeedsReset)
    {
        m_pendingTerminalOutput.append(kTerminalOutputResync);
        m_terminalOutputNeedsReset = false;
    }

    while (!m_pendingTerminalOutput.isEmpty())
    {
        const int bytes = qMin(kTerminalOutputBatchBytes, m_pendingTerminalOutput.size());
        const QByteArray batch = m_pendingTerminalOutput.left(bytes);
        m_pendingTerminalOutput.remove(0, bytes);
        emit terminalOutput(batch);
    }
}


void WebRtcCtl::flushTerminalOutput()
{
    if (m_pendingTerminalOutput.isEmpty())
    {
        updateTerminalFlowControl();
        return;
    }

    const int bytes = qMin(kTerminalOutputBatchBytes, m_pendingTerminalOutput.size());
    const QByteArray batch = m_pendingTerminalOutput.left(bytes);
    m_pendingTerminalOutput.remove(0, bytes);
    m_terminalOutputInFlight += batch.size();
    emit terminalOutput(batch);

    if (!m_pendingTerminalOutput.isEmpty())
        m_terminalOutputFlushTimer->start();
    updateTerminalFlowControl();
}


void WebRtcCtl::acknowledgeTerminalOutput(qint64 bytes)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    m_terminalOutputInFlight = qMax<qint64>(0, m_terminalOutputInFlight - qMax<qint64>(0, bytes));
    updateTerminalFlowControl();
}


void WebRtcCtl::setTerminalConsumerBacklog(qint64 bytes)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    m_terminalConsumerBacklog = qMax<qint64>(0, bytes);
    updateTerminalFlowControl();
}


void WebRtcCtl::updateTerminalFlowControl()
{
    const auto flowRelevantBytes = [](qint64 bytes) {
        return qBound<qint64>(0, bytes, kTerminalOutputHighWatermark);
    };
    const qint64 queuedBytes = flowRelevantBytes(m_fileTextIngressBytes.load()) +
                               flowRelevantBytes(m_pendingTerminalOutput.size()) +
                               flowRelevantBytes(m_terminalOutputInFlight) +
                               flowRelevantBytes(m_terminalConsumerBacklog);
    if (!m_terminalFlowPaused && queuedBytes >= kTerminalOutputHighWatermark)
    {
        m_terminalFlowPaused = true;
        sendTerminalFlowControl(false);
    }
    else if (m_terminalFlowPaused && queuedBytes <= kTerminalOutputLowWatermark)
    {
        m_terminalFlowPaused = false;
        sendTerminalFlowControl(true);
    }
}


void WebRtcCtl::sendTerminalFlowControl(bool enabled)
{
    QJsonObject message = JsonUtil::createObject()
                              .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_FLOW_CONTROL)
                              .add(Constant::KEY_ENABLED, enabled)
                              .build();
    fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(message).toStdString()));
}
