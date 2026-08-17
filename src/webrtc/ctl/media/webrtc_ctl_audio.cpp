#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"

namespace
{
bool sendsMicrophoneAudio(const QString &mode)
{
    return mode == QStringLiteral("call");
}

bool receivesRemoteAudio(const QString &mode)
{
    return mode == QStringLiteral("listen") || mode == QStringLiteral("call");
}
}

void WebRtcCtl::setAudioMode(const QString &mode)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    QString normalized = mode.toLower();
    if (normalized != QStringLiteral("listen") && normalized != QStringLiteral("call"))
        normalized = QStringLiteral("off");

    if (m_audioMode == normalized)
        return;

    const bool needTrack = sendsMicrophoneAudio(normalized) && !m_localAudioTrack;
    m_audioMode = normalized;

    if (needTrack && !ensureAudioTrack())
        LOG_WARN("Audio mode {} selected but audio track could not be created", m_audioMode);

    if (m_peerConnection)
        m_peerConnection->setAudioRecordingMode(false);

    if (m_localAudioTrack)
        m_localAudioTrack->setEnabled(sendsMicrophoneAudio(m_audioMode));
    if (m_remoteAudioTrack)
        m_remoteAudioTrack->setEnabled(receivesRemoteAudio(m_audioMode));

    LOG_INFO("Control audio mode changed: {}", m_audioMode);
}

bool WebRtcCtl::ensureAudioTrack()
{
    if (m_isOnlyFile || !m_peerConnection)
        return false;
    if (m_localAudioTrack)
        return true;

    try
    {
        const std::string audioName = Constant::TYPE_AUDIO.toStdString();
        rtc::Description::Audio audioDesc(audioName);
        audioDesc.addSSRC(3, audioName, Constant::TYPE_VIDEO_MSID.toStdString(), audioName);
        audioDesc.setDirection(rtc::Description::Direction::SendRecv);
        m_localAudioTrack = m_peerConnection->addTrack(audioDesc);
        if (!m_localAudioTrack)
            return false;
        m_peerConnection->setAudioRecordingMode(false);
        m_localAudioTrack->setEnabled(sendsMicrophoneAudio(m_audioMode));
        LOG_INFO("Control audio track created through libwebrtc audio device, mode={}", m_audioMode);
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_WARN("Failed to create control audio track: {}", e.what());
    }
    catch (...)
    {
        LOG_WARN("Failed to create control audio track: unknown error");
    }
    return false;
}

void WebRtcCtl::startAudioPlayback()
{
    
}

void WebRtcCtl::stopAudioPlayback()
{
    
}

void WebRtcCtl::startAudioCapture()
{
    if (m_localAudioTrack)
        m_localAudioTrack->setEnabled(sendsMicrophoneAudio(m_audioMode));
}

void WebRtcCtl::stopAudioCapture()
{
    if (m_localAudioTrack && sendsMicrophoneAudio(m_audioMode))
        m_localAudioTrack->setEnabled(false);
}
