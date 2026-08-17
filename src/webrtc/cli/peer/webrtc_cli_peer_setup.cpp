#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/qt/qt_callback_util.h"


void WebRtcCli::createTracksAndChannels()
{
    if (!m_peerConnection)
    {
        LOG_ERROR("PeerConnection not available for creating tracks");
        return;
    }

    try
    {
        if (!m_isOnlyFile)
        {
            LOG_INFO("Creating libwebrtc desktop video track");
            const std::string videoName = Constant::TYPE_VIDEO.toStdString();
            rtc::Description::Video videoDesc(videoName);
            videoDesc.addSSRC(1, videoName, Constant::TYPE_VIDEO_MSID.toStdString(), videoName);
            videoDesc.setDirection(rtc::Description::Direction::SendOnly);
            videoDesc.setDesktopSourceIndex(m_currentDesktopSourceIndex);
            if (m_currentDesktopSourceHasId)
                videoDesc.setDesktopSourceId(m_currentDesktopSourceId);
            videoDesc.setDesktopFps(effectiveCaptureFps());
            videoDesc.setDesktopQualityProfile(m_qualityProfile.toStdString());
            videoDesc.setDesktopTargetResolution(m_visible_width, m_visible_height);
            const bool useSfuSimulcast = m_mediaTopology.toLower() == QStringLiteral("sfu");
            videoDesc.setDesktopSimulcastRequested(useSfuSimulcast);
            LOG_INFO("Desktop video simulcast request: topology={}, qualityProfile={}, requested={}",
                     m_mediaTopology,
                     m_qualityProfile,
                     useSfuSimulcast);
            m_videoTrack = m_peerConnection->addTrack(videoDesc);
            if (m_videoTrack)
                m_videoTrack->setAiranCaptureCallback(this);

            if (m_audioMode != QStringLiteral("off"))
            {
                LOG_INFO("Creating libwebrtc audio track for requested mode={}", m_audioMode);
                if (ensureAudioTrack())
                    setAudioCaptureEnabled(m_audioMode != QStringLiteral("off"));
            }
            else
            {
                LOG_INFO("Skipping client audio track creation because audio mode is off");
            }

            
            LOG_INFO("Creating input data channel");
            m_inputChannel = m_peerConnection->createDataChannel(Constant::TYPE_INPUT.toStdString());
            setupInputChannelCallbacks();

            LOG_INFO("Creating low-latency mouse movement data channel");
            m_inputMoveChannel = m_peerConnection->createDataChannel(
                Constant::TYPE_INPUT_MOVE.toStdString(), {rtc::Reliability{true, 0}});
            setupInputMoveChannelCallbacks();
        }

        LOG_INFO("Creating file data channel");
        m_fileChannel = m_peerConnection->createDataChannel(Constant::TYPE_FILE.toStdString());
        setupFileChannelCallbacks();

        LOG_INFO("Creating file text data channel");
        m_fileTextChannel = m_peerConnection->createDataChannel(Constant::TYPE_FILE_TEXT.toStdString());
        setupFileTextChannelCallbacks();

        LOG_INFO("Creating clipboard data channel");
        m_clipboardChannel = m_peerConnection->createDataChannel(Constant::TYPE_CLIPBOARD.toStdString());
        setupClipboardChannelCallbacks();

        LOG_INFO("Creating session heartbeat data channel");
        m_heartbeatChannel = m_peerConnection->createDataChannel(Constant::TYPE_SESSION_HEARTBEAT.toStdString(), {rtc::Reliability{true, 0}});
        setupHeartbeatChannelCallbacks();

        m_channelsReady = true;
        LOG_INFO("All tracks and channels created successfully");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to create tracks and channels: {}", e.what());
        sendSignalingError(tr("Remote media track or data channel creation failed: %1").arg(QString::fromUtf8(e.what())));
    }
    catch (...)
    {
        LOG_ERROR("Unknown error during tracks and channels creation");
        sendSignalingError(tr("Remote media track or data channel creation failed: unknown error"));
    }
}


void WebRtcCli::setupCallbacks()
{
    if (!m_peerConnection)
        return;

    m_peerConnection->onStateChange(makeWeakCallback(this, &WebRtcCli::onPeerConnectionStateChanged, m_callbackLifetime));
    m_peerConnection->onIceStateChange(makeWeakCallback(this, &WebRtcCli::onPeerIceStateChanged, m_callbackLifetime));
    m_peerConnection->onGatheringStateChange(makeWeakCallback(this, &WebRtcCli::onPeerGatheringStateChanged, m_callbackLifetime));
    m_peerConnection->onLocalDescription(makeWeakCallback(this, &WebRtcCli::onPeerLocalDescription, m_callbackLifetime));
    m_peerConnection->onLocalCandidate(makeWeakCallback(this, &WebRtcCli::onPeerLocalCandidate, m_callbackLifetime));
}
