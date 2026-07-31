#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QApplication>
#include <QCoreApplication>
#include <QOperatingSystemVersion>

namespace
{

bool sendsMicrophoneAudio(const QString &mode)
{
    return mode == QStringLiteral("listen") || mode == QStringLiteral("call");
}


bool sendsRemoteSystemAudio(const QString &mode)
{
    return mode == QStringLiteral("listen");
}


QString normalizeAudioMode(const QString &mode)
{
    const QString normalized = mode.toLower();
    if (normalized == QStringLiteral("listen") || normalized == QStringLiteral("call"))
        return normalized;
    return QStringLiteral("off");
}


QString audioModeLabel(const QString &mode)
{
    if (mode == QStringLiteral("listen"))
        return QCoreApplication::translate("WebRtcCli", "Listen");
    if (mode == QStringLiteral("call"))
        return QCoreApplication::translate("WebRtcCli", "Call");
    return QCoreApplication::translate("WebRtcCli", "Close");
}

bool supportsRemoteSystemAudio()
{
#if defined(Q_OS_MACOS)
    return QOperatingSystemVersion::current() >=
           QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13);
#else
    return true;
#endif
}
} /* namespace */


void WebRtcCli::setAudioCaptureEnabled(bool enabled)
{
    const bool wasEnabled = m_audioCaptureEnabled;
    m_audioCaptureEnabled = enabled;
    const bool sendAudio = enabled && m_audioTrack &&
                           (sendsRemoteSystemAudio(m_audioMode) || sendsMicrophoneAudio(m_audioMode));

    if (m_peerConnection)
        m_peerConnection->setAudioRecordingMode(sendsRemoteSystemAudio(m_audioMode));

    if (m_audioTrack)
        m_audioTrack->setEnabled(sendAudio);

    if (wasEnabled == enabled && !sendAudio)
        return;

    if (enabled && (sendsRemoteSystemAudio(m_audioMode) || sendsMicrophoneAudio(m_audioMode)) && !m_audioTrack)
        LOG_INFO("Client audio mode is enabled but no audio track exists; waiting for audio-enabled PeerConnection");

    LOG_INFO("Client audio track enabled={}, systemMode={}, microphoneMode={}",
             sendAudio, sendsRemoteSystemAudio(m_audioMode), sendsMicrophoneAudio(m_audioMode));
}


bool WebRtcCli::ensureAudioTrack()
{
    if (m_isOnlyFile || !m_peerConnection)
        return false;
    if (m_audioTrack)
        return true;

    try
    {
        const std::string audioName = Constant::TYPE_AUDIO.toStdString();
        rtc::Description::Audio audioDesc(audioName);
        audioDesc.addSSRC(2, audioName, Constant::TYPE_VIDEO_MSID.toStdString(), audioName);
        audioDesc.setDirection(m_audioMode == QStringLiteral("call")
                                   ? rtc::Description::Direction::SendRecv
                                   : rtc::Description::Direction::SendOnly);
        audioDesc.setSystemLoopback(m_audioMode == QStringLiteral("listen"));
        m_audioTrack = m_peerConnection->addTrack(audioDesc);
        if (!m_audioTrack)
            return false;
        m_audioTrack->setEnabled(false);
        m_audioTrack->onFrame(makeWeakCallback(this, &WebRtcCli::onAudioFrameReceived, m_callbackLifetime));
        LOG_INFO("Client audio track created through libwebrtc audio device, mode={}", m_audioMode);
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_WARN("Failed to create client audio track: {}", e.what());
    }
    catch (...)
    {
        LOG_WARN("Failed to create client audio track: unknown error");
    }
    return false;
}


void WebRtcCli::setAudioMode(const QString &mode)
{
    const QString normalized = normalizeAudioMode(mode);
    if (m_audioMode == normalized)
    {
        if (normalized != QStringLiteral("off"))
        {
            if (!m_audioTrack && !ensureAudioTrack())
                LOG_WARN("Client audio mode {} is active but audio track could not be created", normalized);
            setAudioCaptureEnabled(sendsRemoteSystemAudio(m_audioMode) || sendsMicrophoneAudio(m_audioMode));
        }
        else
        {
            setAudioCaptureEnabled(false);
        }
        return;
    }

    const bool wasOff = m_audioMode == QStringLiteral("off");
    const bool needTrack = normalized != QStringLiteral("off") && !m_audioTrack;
    m_audioMode = normalized;
    if (needTrack && !ensureAudioTrack())
        LOG_WARN("Client audio mode {} selected but audio track could not be created", m_audioMode);
    setAudioCaptureEnabled(sendsRemoteSystemAudio(m_audioMode) || sendsMicrophoneAudio(m_audioMode));
    if (wasOff && normalized != QStringLiteral("off") && m_peerConnection && m_audioTrack)
    {
        try
        {
            m_peerConnection->setLocalDescription(rtc::Description::Type::Offer);
            LOG_INFO("Client audio mode requires renegotiation offer: {}", m_audioMode);
        }
        catch (const std::exception &e)
        {
            LOG_WARN("Client audio renegotiation offer failed: {}", e.what());
        }
    }
    LOG_INFO("Client audio mode changed: {}", m_audioMode);
}


void WebRtcCli::startAudioCapture()
{
    setAudioCaptureEnabled(sendsRemoteSystemAudio(m_audioMode) || sendsMicrophoneAudio(m_audioMode));
}


void WebRtcCli::stopAudioCapture()
{
    if (m_audioTrack)
        m_audioTrack->setEnabled(false);
    m_audioCaptureEnabled = false;
}


void WebRtcCli::sendAudioCaptureResponse(const QString &requestId, const QString &mode, bool accepted, const QString &message)
{
    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_AUDIO_CAPTURE)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, m_remoteId)
                          .add(Constant::KEY_STATUS_ONLY, true)
                          .add(Constant::KEY_REQUEST_ID, requestId)
                          .add(Constant::KEY_ACCEPTED, accepted)
                          .add(Constant::KEY_ENABLED, accepted && mode != QStringLiteral("off"))
                          .add(Constant::KEY_AUDIO_MODE, mode)
                          .add(Constant::KEY_MESSAGE, message)
                          .build();
    sendInputChannelMessage(obj);
    LOG_INFO("Audio mode response sent: mode={}, accepted={}, requestId={}", mode, accepted, requestId);
}


void WebRtcCli::applyAudioCaptureDecision(const QString &requestId, const QString &requestedMode, bool accepted, const QString &message)
{
    const QString appliedMode = accepted ? requestedMode : QStringLiteral("off");
    setAudioMode(appliedMode);
    sendAudioCaptureResponse(requestId, appliedMode, accepted, message);
}


void WebRtcCli::handleAudioCaptureConfig(const QJsonObject &object)
{
    if (JsonUtil::getBool(object, Constant::KEY_STATUS_ONLY, false))
        return;

    const bool enabled = JsonUtil::getBool(object, Constant::KEY_ENABLED, false);
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    const QString requestedMode = normalizeAudioMode(JsonUtil::getString(object, Constant::KEY_AUDIO_MODE,
                                                                        enabled ? QStringLiteral("call") : QStringLiteral("off")));

    if (requestedMode == QStringLiteral("off"))
    {
        setAudioMode(QStringLiteral("off"));
        sendAudioCaptureResponse(requestId, QStringLiteral("off"), true, QStringLiteral("Audio disabled."));
        return;
    }

    if (requestedMode == QStringLiteral("listen") && !supportsRemoteSystemAudio())
    {
        applyAudioCaptureDecision(requestId,
                                  requestedMode,
                                  false,
                                  tr("System audio capture requires macOS 13 or later."));
        return;
    }

    if (!RuntimeEnvironment::uiAvailable() || !QApplication::instance())
    {
        applyAudioCaptureDecision(requestId, requestedMode, true, QStringLiteral("Accepted without prompt."));
        return;
    }

    emit audioCapturePromptRequested(requestId, requestedMode, audioModeLabel(requestedMode));
}


void WebRtcCli::startAudioPlayback()
{
    
}


void WebRtcCli::stopAudioPlayback()
{
    
}


void WebRtcCli::onAudioFrameReceived(rtc::binary data, rtc::FrameInfo info)
{
    noteSessionInboundActivity();
    noteSessionTransportProgress();
    Q_UNUSED(data);
    Q_UNUSED(info);
    LOG_TRACE("Remote audio frame callback received; playback is handled by the libwebrtc audio device");
}
