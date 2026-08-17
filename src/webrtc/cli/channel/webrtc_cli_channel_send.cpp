#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/json/json_util.h"

namespace
{
constexpr int kMaxPendingFileTextMessages = 256;
constexpr qint64 kMaxPendingFileTextMessageBytes = 2LL * 1024 * 1024;
constexpr qint64 kMaxFileTextChannelMessageBytes = 16LL * 1024 * 1024;
constexpr int kMaxPendingInputMessages = 128;
constexpr qint64 kMaxPendingInputMessageBytes = 512 * 1024;
}

void WebRtcCli::queueFileTextMessage(const QJsonObject &message)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    const qint64 bytes = JsonUtil::toCompactString(message).toUtf8().size();
    if (m_pendingFileTextMessages.size() >= kMaxPendingFileTextMessages ||
        bytes > kMaxPendingFileTextMessageBytes - m_pendingFileTextMessageBytes)
    {
        LOG_ERROR("CLI file-text send queue overflow; destroying the partial session instead of dropping protocol data: size={}, queuedBytes={}, queuedMessages={}",
                  bytes, m_pendingFileTextMessageBytes, m_pendingFileTextMessages.size());
        m_disconnectReason = QStringLiteral("file_text_send_queue_overflow");
        emit destroyCli();
        return;
    }
    m_pendingFileTextMessages.enqueue(message);
    m_pendingFileTextMessageBytes += bytes;
}

void WebRtcCli::flushPendingFileTextMessages()
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (!m_connected || !m_fileTextChannel || !m_fileTextChannel->isOpen())
        return;
    while (!m_pendingFileTextMessages.isEmpty())
    {
        const QJsonObject &message = m_pendingFileTextMessages.head();
        const QString jsonStr = JsonUtil::toCompactString(message);
        bool sent = false;
        try
        {
            sent = m_fileTextChannel->send(jsonStr.toStdString());
        }
        catch (...)
        {
            sent = false;
        }
        if (!sent)
            return;
        noteSessionOutboundActivity();
        m_pendingFileTextMessageBytes -= jsonStr.toUtf8().size();
        m_pendingFileTextMessages.dequeue();
    }
}

void WebRtcCli::queueInputMessage(const QJsonObject &message)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    const qint64 bytes = JsonUtil::toCompactString(message).toUtf8().size();
    if (m_pendingInputMessages.size() >= kMaxPendingInputMessages ||
        bytes > kMaxPendingInputMessageBytes - m_pendingInputMessageBytes)
    {
        LOG_ERROR("CLI input send queue overflow; destroying the partial session instead of dropping protocol data: size={}, queuedBytes={}, queuedMessages={}",
                  bytes, m_pendingInputMessageBytes, m_pendingInputMessages.size());
        m_disconnectReason = QStringLiteral("input_send_queue_overflow");
        emit destroyCli();
        return;
    }
    m_pendingInputMessages.enqueue(message);
    m_pendingInputMessageBytes += bytes;
}

void WebRtcCli::flushPendingInputMessages()
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (!m_connected || !m_inputChannel || !m_inputChannel->isOpen())
        return;
    while (!m_pendingInputMessages.isEmpty())
    {
        const QJsonObject &message = m_pendingInputMessages.head();
        const QString jsonStr = JsonUtil::toCompactString(message);
        bool sent = false;
        try
        {
            sent = m_inputChannel->send(jsonStr.toStdString());
        }
        catch (...)
        {
            sent = false;
        }
        if (!sent)
            return;
        noteSessionOutboundActivity();
        m_pendingInputMessageBytes -= jsonStr.toUtf8().size();
        m_pendingInputMessages.dequeue();
    }
}


void WebRtcCli::sendFileChannelMessage(const QJsonObject &message)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (!m_connected || !m_fileChannel || !m_fileChannel->isOpen())
    {
        LOG_ERROR("File channel not available");
        return;
    }

    const QString jsonStr = JsonUtil::toCompactString(message);
    try
    {
        const bool sent = m_fileChannel->send(jsonStr.toStdString());
        if (sent)
            noteSessionOutboundActivity();
        else
            LOG_WARN("File channel send returned false; message was not accepted");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to send file channel message: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to send file channel message: unknown error");
    }
}


bool WebRtcCli::sendFileTextChannelMessage(const QJsonObject &message)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return false;

    const QString msgType = JsonUtil::getString(message, Constant::KEY_MSGTYPE);
    const QByteArray jsonBytes = JsonUtil::toCompactBytes(message);
    if (jsonBytes.size() > kMaxFileTextChannelMessageBytes)
    {
        LOG_ERROR("Rejected oversized outbound file-text message: type={}, size={}",
                  msgType, jsonBytes.size());
        m_disconnectReason = QStringLiteral("file_text_message_too_large");
        emit destroyCli();
        return false;
    }
    if (!m_connected || !m_fileTextChannel || !m_fileTextChannel->isOpen())
    {
        if (msgType != Constant::TYPE_TERMINAL_OUTPUT)
            queueFileTextMessage(message);
        return false;
    }

    if (!m_pendingFileTextMessages.isEmpty())
    {
        flushPendingFileTextMessages();
        if (!m_pendingFileTextMessages.isEmpty())
        {
            if (msgType != Constant::TYPE_TERMINAL_OUTPUT)
                queueFileTextMessage(message);
            return false;
        }
    }

    const QString jsonStr = JsonUtil::toCompactString(message);
    try
    {
        const bool sent = m_fileTextChannel->send(jsonStr.toStdString());
        if (!sent)
        {
            LOG_WARN("File text channel send returned false: type={}, size={} bytes, buffered={} bytes",
                     msgType, jsonStr.toUtf8().size(), m_fileTextChannel->bufferedAmount());
        }
        else if (msgType == Constant::TYPE_TERMINAL_OUTPUT)
        {
            LOG_TRACE("Terminal output message sent to controller: size={} bytes, buffered={} bytes",
                      jsonStr.toUtf8().size(), m_fileTextChannel->bufferedAmount());
        }
        else
        {
            LOG_TRACE("Sent file text channel message type={}, size={} bytes", msgType, jsonStr.toUtf8().size());
        }
        if (sent)
            noteSessionOutboundActivity();
        else if (msgType != Constant::TYPE_TERMINAL_OUTPUT)
            queueFileTextMessage(message);
        return sent;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to send file text channel message: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to send file text channel message: unknown error");
    }
    if (msgType != Constant::TYPE_TERMINAL_OUTPUT)
        queueFileTextMessage(message);
    return false;
}


bool WebRtcCli::sendInputChannelMessage(const QJsonObject &message)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return false;

    if (!m_connected || !m_inputChannel || !m_inputChannel->isOpen())
    {
        queueInputMessage(message);
        return false;
    }
    if (!m_pendingInputMessages.isEmpty())
    {
        flushPendingInputMessages();
        if (!m_pendingInputMessages.isEmpty())
        {
            queueInputMessage(message);
            return false;
        }
    }

    const QString jsonStr = JsonUtil::toCompactString(message);
    try
    {
        const bool sent = m_inputChannel->send(jsonStr.toStdString());
        if (!sent)
        {
            LOG_WARN("Input channel send returned false: size={} bytes, buffered={} bytes",
                     jsonStr.toUtf8().size(), m_inputChannel->bufferedAmount());
            queueInputMessage(message);
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
    queueInputMessage(message);
    return false;
}
