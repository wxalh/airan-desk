#include "webrtc/ctl/webrtc_ctl.h"


void WebRtcCtl::fileChannelSendMsg(const rtc::message_variant &data)
{
    LOG_TRACE("fileChannelSendMsg called - connected: {}, fileChannel: {}, isOpen: {}",
              m_connected,
              (m_fileChannel != nullptr),
              (m_fileChannel && m_fileChannel->isOpen()));

    if (m_connected && m_fileChannel && m_fileChannel->isOpen())
    {
        try
        {
            m_fileChannel->send(data);
            noteSessionOutboundActivity();
            LOG_TRACE("Successfully sent file channel message");
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
    LOG_TRACE("fileTextChannelSendMsg called - connected: {}, fileTextChannel: {}, isOpen: {}",
              m_connected,
              (m_fileTextChannel != nullptr),
              (m_fileTextChannel && m_fileTextChannel->isOpen()));

    if (m_connected && m_fileTextChannel && m_fileTextChannel->isOpen())
    {
        try
        {
            m_fileTextChannel->send(data);
            noteSessionOutboundActivity();
            LOG_TRACE("Successfully sent file text channel message");
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to send file text channel message: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Failed to send file text channel message: unknown error");
        }
    }
    else
    {
        LOG_WARN("File text channel not ready for sending - connected: {}, channel exists: {}, channel open: {}",
                 m_connected,
                 (m_fileTextChannel != nullptr),
                 (m_fileTextChannel && m_fileTextChannel->isOpen()));
    }
}
