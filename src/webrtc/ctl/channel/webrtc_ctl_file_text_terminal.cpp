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
}


bool WebRtcCtl::handleTerminalTextChannelObject(const QJsonObject &object, const QString &msgType)
{
    if (msgType == Constant::TYPE_TERMINAL_OUTPUT)
    {
        const QString encodedData = JsonUtil::getString(object, Constant::KEY_DATA);
        if (encodedData.size() > kMaxTerminalOutputMessageEncodedBytes)
        {
            LOG_WARN("Rejected oversized terminal output message: encodedSize={} bytes", encodedData.size());
            sendTerminalFlowControl(false);
            return true;
        }
        const QByteArray bytes = QByteArray::fromBase64(encodedData.toLatin1());
        if (bytes.size() > kMaxPendingTerminalOutputBytes - m_pendingTerminalOutput.size())
        {
            LOG_WARN("Dropped terminal output because the pending buffer is full: size={}, pending={}",
                     bytes.size(), m_pendingTerminalOutput.size());
            sendTerminalFlowControl(false);
            return true;
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
                          JsonUtil::getBool(object, Constant::KEY_PATH_TRACKING_READY, false));
        return true;
    }
    if (msgType == Constant::TYPE_TERMINAL_CLOSED)
    {
        emit terminalClosed(JsonUtil::getInt(object, Constant::KEY_STATUS, 0));
        return true;
    }
    if (msgType == Constant::TYPE_TERMINAL_ERROR)
    {
        emit terminalError(JsonUtil::getString(object, Constant::KEY_ERROR));
        return true;
    }
    return false;
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
    m_terminalOutputInFlight = qMax<qint64>(0, m_terminalOutputInFlight - qMax<qint64>(0, bytes));
    updateTerminalFlowControl();
}


void WebRtcCtl::setTerminalConsumerBacklog(qint64 bytes)
{
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
