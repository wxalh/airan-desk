#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/json/json_util.h"


void WebRtcCli::sendFileChannelMessage(const QJsonObject &message)
{
    if (!m_connected || !m_fileChannel || !m_fileChannel->isOpen())
    {
        LOG_ERROR("File channel not available");
        return;
    }

    const QString jsonStr = JsonUtil::toCompactString(message);
    try
    {
        m_fileChannel->send(jsonStr.toStdString());
        noteSessionOutboundActivity();
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
    if (!m_connected || !m_fileTextChannel || !m_fileTextChannel->isOpen())
    {
        LOG_ERROR("File text channel not available");
        return false;
    }

    const QString jsonStr = JsonUtil::toCompactString(message);
    try
    {
        const bool sent = m_fileTextChannel->send(jsonStr.toStdString());
        const QString msgType = JsonUtil::getString(message, Constant::KEY_MSGTYPE);
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
    return false;
}


bool WebRtcCli::sendInputChannelMessage(const QJsonObject &message)
{
    if (!m_inputChannel || !m_inputChannel->isOpen())
    {
        LOG_DEBUG("Input channel not available");
        return false;
    }

    const QString jsonStr = JsonUtil::toCompactString(message);
    try
    {
        const bool sent = m_inputChannel->send(jsonStr.toStdString());
        if (!sent)
        {
            LOG_WARN("Input channel send returned false: size={} bytes, buffered={} bytes",
                     jsonStr.toUtf8().size(), m_inputChannel->bufferedAmount());
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
