#include "webrtc/cli/webrtc_cli.h"
#include "webrtc/cli/media/webrtc_cli_media_labels.h"

#include "common/constant.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"


void WebRtcCli::setDesktopLocked(bool locked)
{
    const bool changed = m_desktopLocked != locked;
    if (!changed && locked)
        return;

    m_desktopLocked = locked;
    notifyDesktopState();
    if (locked)
    {
        if (m_videoTrack)
        {
            m_captureRecoveryHoldUntilMs = QDateTime::currentMSecsSinceEpoch() + 12000;
            m_videoTrack->reconfigureDesktopCaptureOptions();
            m_videoTrack->requestKeyFrame();
            LOG_INFO("Desktop capture secure fallback refresh requested after Windows session lock");
        }
    }
    else
    {
        recoverDesktopCaptureAfterSessionUnlock();
    }
}

void WebRtcCli::recoverDesktopCaptureAfterSessionUnlock()
{
    if (m_isOnlyFile || !m_videoTrack)
        return;

    const bool hadAdaptiveFpsLimit = m_adaptiveVideoFpsCap > 0 || m_adaptiveCpuFpsCap > 0;
    m_adaptiveVideoFpsCap = 0;
    m_adaptiveFpsPoorScore = 0;
    m_adaptiveFpsGoodScore = 0;
    m_lastAdaptiveFpsSwitchMs = 0;
    m_adaptiveCpuFpsCap = 0;
    m_adaptiveCpuPoorScore = 0;
    m_adaptiveCpuGoodScore = 0;
    m_lastAdaptiveCpuFpsSwitchMs = 0;
    m_captureRecoveryHoldUntilMs = QDateTime::currentMSecsSinceEpoch() + 12000;

    const bool reconfigured = m_videoTrack->reconfigureDesktopCaptureOptions();
    applyEffectiveVideoFpsIfNeeded("session-unlock");
    if (reconfigured)
    {
        m_videoTrack->requestKeyFrame();
        LOG_INFO("Desktop capture recovery requested after Windows session unlock; adaptiveFpsReset={}",
                 hadAdaptiveFpsLimit ? "true" : "false");
    }
}

void WebRtcCli::handleDesktopScreensChanged()
{
    if (m_isOnlyFile)
        return;

    const bool hadAdaptiveFpsLimit = m_adaptiveVideoFpsCap > 0 || m_adaptiveCpuFpsCap > 0;
    m_adaptiveVideoFpsCap = 0;
    m_adaptiveFpsPoorScore = 0;
    m_adaptiveFpsGoodScore = 0;
    m_lastAdaptiveFpsSwitchMs = 0;
    m_adaptiveCpuFpsCap = 0;
    m_adaptiveCpuPoorScore = 0;
    m_adaptiveCpuGoodScore = 0;
    m_lastAdaptiveCpuFpsSwitchMs = 0;
    m_captureRecoveryHoldUntilMs = QDateTime::currentMSecsSinceEpoch() + 12000;

    ++m_screenCatalogGeneration;
    const bool currentScreenStillExists = screenIndexForId(m_screenId) >= 0;
    if (!currentScreenStillExists)
    {
        const QJsonArray screens = screenCatalogJson();
        const QString firstScreenId = screens.isEmpty()
                                          ? QStringLiteral("screen-0")
                                          : screens.first().toObject().value(Constant::KEY_SCREEN_ID).toString();
        selectScreenById(firstScreenId, m_videoTrack != nullptr);
    }
    else
    {
        selectScreenById(m_screenId, m_videoTrack != nullptr);
    }
    if (m_videoTrack)
    {
        m_videoTrack->setDesktopTargetResolution(m_visible_width, m_visible_height, effectiveCaptureFps());
        const bool reconfigured = m_videoTrack->reconfigureDesktopCaptureOptions();
        m_videoTrack->requestKeyFrame();
        LOG_DEBUG("Desktop capture refresh requested after screen catalog change: reconfigured={}, adaptiveFpsReset={}",
                  reconfigured ? "true" : "false",
                  hadAdaptiveFpsLimit ? "true" : "false");
    }
    notifyCurrentStreamConfig();
    LOG_DEBUG("Desktop screen catalog change handled: generation={}, currentScreenId={}",
              m_screenCatalogGeneration,
              m_screenId);
}

void WebRtcCli::notifyDesktopState()
{
    if (m_isOnlyFile || !m_inputChannel || !m_inputChannel->isOpen())
        return;

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_DESKTOP_STATE)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, m_remoteId)
                          .add(Constant::KEY_LOCKED, m_desktopLocked)
                          .add(Constant::KEY_MESSAGE, m_desktopLocked
                                                       ? tr("Remote desktop is locked.")
                                                       : tr("Remote desktop is unlocked."))
                          .build();
    sendInputChannelMessage(obj);
    LOG_INFO("Notified control side desktop lock state: {}", m_desktopLocked);
}


void WebRtcCli::notifyCurrentStreamConfig()
{
    constexpr qint64 kMinStatusNotifyIntervalMs = 500;

    if (m_isOnlyFile)
        return;
    if (!m_inputChannel || !m_inputChannel->isOpen())
    {
        LOG_DEBUG("Stream config notification delayed until input channel is open");
        return;
    }

    if (m_currentCaptureMethod.isEmpty())
        m_currentCaptureMethod = webrtc_cli_internal::desktopCaptureMethodLabel();
    if (m_currentCaptureBackend.isEmpty())
        m_currentCaptureBackend = m_currentCaptureMethod;
    if (m_currentCapturePath.isEmpty())
        m_currentCapturePath = QStringLiteral("unknown");
    if (m_currentEncodePath.isEmpty())
        m_currentEncodePath = QStringLiteral("unknown");
    if (m_currentFallbackReason.isEmpty())
        m_currentFallbackReason = QStringLiteral("?");
    if (m_currentEncoderName.isEmpty())
        m_currentEncoderName = webrtc_cli_internal::readableEncoderName(QString(), m_negotiatedVideoCodec);
    if (m_currentEncoderType.isEmpty())
        m_currentEncoderType = QStringLiteral("hardware_preferred");

    m_screenId = currentScreenId();
    const QJsonArray screens = screenCatalogJson();
    const QString screensSignature = QString::fromUtf8(QJsonDocument(screens).toJson(QJsonDocument::Compact));

    const QString signature = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17|%18|%19|%20|%21|%22|%23")
                                  .arg(m_networkPath)
                                  .arg(m_mediaTopology)
                                  .arg(m_qualityProfile)
                                  .arg(m_coded_width)
                                  .arg(m_coded_height)
                                  .arg(m_fps)
                                  .arg(m_negotiatedVideoCodec)
                                  .arg(m_currentCaptureMethod)
                                  .arg(m_currentEncoderName)
                                  .arg(m_currentEncoderType)
                                  .arg(m_screenId)
                                  .arg(m_screenCatalogGeneration)
                                  .arg(screensSignature)
                                  .arg(m_currentCaptureBackend)
                                  .arg(m_currentCapturePath)
                                  .arg(m_currentEncodePath)
                                  .arg(m_currentFallbackReason)
                                  .arg(m_fps)
                                  .arg(m_adaptiveVideoFpsCap)
                                  .arg(m_adaptiveCpuFpsCap)
                                  .arg(ConfigUtil->enable_wgc_capture)
                                  .arg(ConfigUtil->enable_dxgi_capture)
                                  .arg(ConfigUtil->enable_dxgi_native_gpu_capture);
    if (signature == m_lastStreamConfigSignature)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsedMs = nowMs - m_lastStreamConfigNotifyMs;
    if (m_lastStreamConfigNotifyMs > 0 && elapsedMs < kMinStatusNotifyIntervalMs)
    {
        if (m_streamConfigNotifyTimer && !m_streamConfigNotifyTimer->isActive())
            m_streamConfigNotifyTimer->start(static_cast<int>(kMinStatusNotifyIntervalMs - elapsedMs));
        return;
    }

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_STREAM_CONFIG)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, m_remoteId)
                          .add(Constant::KEY_STATUS_ONLY, true)
                          .add(Constant::KEY_CODED_WIDTH, m_coded_width)
                          .add(Constant::KEY_CODED_HEIGHT, m_coded_height)
                          .add(Constant::KEY_VISIBLE_WIDTH, m_visible_width)
                          .add(Constant::KEY_VISIBLE_HEIGHT, m_visible_height)
                          .add(Constant::KEY_PAD_LEFT, 0)
                          .add(Constant::KEY_PAD_TOP, 0)
                          .add(Constant::KEY_PAD_RIGHT, qMax(0, m_coded_width - m_visible_width))
                          .add(Constant::KEY_PAD_BOTTOM, qMax(0, m_coded_height - m_visible_height))
                          .add(Constant::KEY_NETWORK_PATH, m_networkPath)
                          .add(Constant::KEY_MEDIA_TOPOLOGY, m_mediaTopology)
                          .add(Constant::KEY_QUALITY_PROFILE, m_qualityProfile)
                          .add(Constant::KEY_FPS, m_fps)
                          .add(Constant::KEY_ENABLE_WGC_CAPTURE, ConfigUtil->enable_wgc_capture)
                          .add(Constant::KEY_ENABLE_DXGI_CAPTURE, ConfigUtil->enable_dxgi_capture)
                          .add(Constant::KEY_ENABLE_DXGI_NATIVE_GPU_CAPTURE, ConfigUtil->enable_dxgi_native_gpu_capture)
                          .add(Constant::KEY_OS, webrtc_cli_internal::localOsName())
                          .add(Constant::KEY_VIDEO_CODEC, m_negotiatedVideoCodec)
                          .add(Constant::KEY_CAPTURE_METHOD, m_currentCaptureMethod)
                          .add(Constant::KEY_CAPTURE_BACKEND, m_currentCaptureBackend)
                          .add(Constant::KEY_CAPTURE_PATH, m_currentCapturePath)
                          .add(Constant::KEY_ENCODE_PATH, m_currentEncodePath)
                          .add(Constant::KEY_FALLBACK_REASON, m_currentFallbackReason)
                          .add(Constant::KEY_ENCODER_NAME, m_currentEncoderName)
                          .add(Constant::KEY_ENCODER_TYPE, m_currentEncoderType)
                          .add(Constant::KEY_SCREEN_ID, m_screenId)
                          .add(Constant::KEY_SCREEN_INDEX, m_screenIndex)
                          .add(Constant::KEY_SCREEN_GENERATION, m_screenCatalogGeneration)
                          .add(Constant::KEY_SCREENS, screens)
                          .build();
    if (sendInputChannelMessage(obj))
    {
        m_lastStreamConfigSignature = signature;
        m_lastStreamConfigNotifyMs = nowMs;
        LOG_DEBUG("Notified control side stream config: codec={}, qualityProfile={}, capture={}({}), encodePath={}, encoder={}, type={}, fallback={}",
                  m_negotiatedVideoCodec,
                  m_qualityProfile,
                  m_currentCaptureMethod,
                  m_currentCaptureBackend,
                  m_currentEncodePath,
                  m_currentEncoderName,
                  m_currentEncoderType,
                  m_currentFallbackReason);
    }
}
