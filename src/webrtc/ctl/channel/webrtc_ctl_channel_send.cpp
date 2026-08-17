#include "webrtc/ctl/webrtc_ctl.h"

#include "util/json/json_util.h"

#include <QUuid>

namespace
{
constexpr int kMaxPendingFileTextMessages = 256;
constexpr qint64 kMaxPendingFileTextMessageBytes = 2LL * 1024 * 1024;
constexpr qint64 kMaxFileTextChannelMessageBytes = 16LL * 1024 * 1024;

qint64 messageSize(const rtc::message_variant &data)
{
    return std::visit([](const auto &value) -> qint64 {
        return static_cast<qint64>(value.size());
    }, data);
}
}


void WebRtcCtl::fileChannelSendMsg(const rtc::message_variant &data)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    LOG_TRACE("fileChannelSendMsg called - connected: {}, fileChannel: {}, isOpen: {}",
              m_connected,
              (m_fileChannel != nullptr),
              (m_fileChannel && m_fileChannel->isOpen()));

    if (m_connected && m_fileChannel && m_fileChannel->isOpen())
    {
        try
        {
            const bool sent = m_fileChannel->send(data);
            if (sent)
            {
                noteSessionOutboundActivity();
                LOG_TRACE("Successfully sent file channel message");
            }
            else
            {
                LOG_WARN("File channel send returned false; message was not accepted");
            }
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
    else
    {
        LOG_WARN("File channel not ready for sending - connected: {}, channel exists: {}, channel open: {}",
                 m_connected,
                 (m_fileChannel != nullptr),
                 (m_fileChannel && m_fileChannel->isOpen()));
    }
}


void WebRtcCtl::fileTextChannelSendMsg(const rtc::message_variant &data)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    LOG_TRACE("fileTextChannelSendMsg called - connected: {}, fileTextChannel: {}, isOpen: {}",
              m_connected,
              (m_fileTextChannel != nullptr),
              (m_fileTextChannel && m_fileTextChannel->isOpen()));

    rtc::message_variant outboundData = data;
    if (std::holds_alternative<std::string>(outboundData))
    {
        QJsonObject object = JsonUtil::safeParseObject(
            QByteArray::fromStdString(std::get<std::string>(outboundData)));
        const QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
        if (msgType == Constant::TYPE_TERMINAL_START)
            beginTerminalStartGeneration(JsonUtil::getString(object, Constant::KEY_REQUEST_ID));
        else if (msgType == Constant::TYPE_FILE_LIST)
        {
            QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID).trimmed();
            if (requestId.isEmpty())
            {
                requestId = QUuid::createUuid().toString();
                requestId.remove(QLatin1Char('{'));
                requestId.remove(QLatin1Char('}'));
            }
            object.insert(Constant::KEY_REQUEST_ID, requestId);
            m_latestFileListRequestId = requestId;
            outboundData = rtc::message_variant(JsonUtil::toCompactBytes(object).toStdString());
        }
    }

    if (messageSize(outboundData) > kMaxFileTextChannelMessageBytes)
    {
        LOG_ERROR("Rejected oversized outbound file-text message: size={}", messageSize(outboundData));
        requestSessionReconnect(tr("File control message is too large, reconnecting..."));
        return;
    }

    const auto queueMessage = [this](const rtc::message_variant &message) {
        const qint64 bytes = messageSize(message);
        if (m_pendingFileTextMessages.size() >= kMaxPendingFileTextMessages ||
            bytes > kMaxPendingFileTextMessageBytes - m_pendingFileTextMessageBytes)
        {
            LOG_ERROR("File-text send queue overflow; reconnecting instead of dropping protocol data: size={}, queuedBytes={}, queuedMessages={}",
                      bytes, m_pendingFileTextMessageBytes, m_pendingFileTextMessages.size());
            requestSessionReconnect(tr("File control queue is full, reconnecting..."));
            return;
        }
        m_pendingFileTextMessages.enqueue(message);
        m_pendingFileTextMessageBytes += bytes;
        LOG_DEBUG("Queued file text message until channel is ready: size={}, queuedBytes={}, queuedMessages={}",
                  bytes, m_pendingFileTextMessageBytes, m_pendingFileTextMessages.size());
    };

    if (m_connected && m_fileTextChannel && m_fileTextChannel->isOpen())
    {
        flushPendingFileTextMessages();
        if (!m_pendingFileTextMessages.isEmpty())
        {
            queueMessage(outboundData);
            return;
        }
        try
        {
            const bool sent = m_fileTextChannel->send(outboundData);
            if (!sent)
            {
                LOG_WARN("File text channel send returned false; keeping message queued: size={}, buffered={}",
                         messageSize(outboundData),
                         m_fileTextChannel->bufferedAmount());
                queueMessage(outboundData);
                return;
            }
            noteSessionOutboundActivity();
            LOG_TRACE("Successfully sent file text channel message");
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to send file text channel message: {}", e.what());
            queueMessage(outboundData);
        }
        catch (...)
        {
            LOG_ERROR("Failed to send file text channel message: unknown error");
            queueMessage(outboundData);
        }
    }
    else
    {
        LOG_DEBUG("File text channel not ready; queueing message - connected: {}, channel exists: {}, channel open: {}",
                 m_connected,
                 (m_fileTextChannel != nullptr),
                 (m_fileTextChannel && m_fileTextChannel->isOpen()));
        queueMessage(outboundData);
    }
}


void WebRtcCtl::flushPendingFileTextMessages()
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (!m_connected || !m_fileTextChannel || !m_fileTextChannel->isOpen())
        return;

    while (!m_pendingFileTextMessages.isEmpty())
    {
        const rtc::message_variant &message = m_pendingFileTextMessages.head();
        try
        {
            const bool sent = m_fileTextChannel->send(message);
            if (!sent)
            {
                LOG_WARN("Failed to flush file text message because DataChannel returned false: buffered={}",
                         m_fileTextChannel->bufferedAmount());
                return;
            }
            noteSessionOutboundActivity();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to flush pending file text message: {}", e.what());
            return;
        }
        catch (...)
        {
            LOG_ERROR("Failed to flush pending file text message: unknown error");
            return;
        }
        m_pendingFileTextMessageBytes -= messageSize(message);
        m_pendingFileTextMessages.dequeue();
    }
    LOG_DEBUG("Flushed pending file text messages");
}
