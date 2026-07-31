#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/config/config_util.h"

#include <QDateTime>

namespace
{

bool isCpuCapturePath(const QString &captureMethod,
                      const QString &captureBackend,
                      const QString &capturePath)
{
    const QString path = capturePath.trimmed();
    const QString method = captureMethod.trimmed().toLower();
    const QString backend = captureBackend.trimmed().toLower();

    if (path.isEmpty() && method.isEmpty() && backend.isEmpty())
        return true;

    const bool explicitGpuCapture = path == QStringLiteral("NativeGpuCapture") ||
                                    method == QStringLiteral("wgc") ||
                                    method == QStringLiteral("dxgi") ||
                                    backend == QStringLiteral("wgc") ||
                                    backend == QStringLiteral("dxgi");
    if ((path.isEmpty() || path == QStringLiteral("unknown")) && !explicitGpuCapture)
        return true;

    if (path == QStringLiteral("WebRtcDerivedCpuCapture") ||
        path == QStringLiteral("CaptureReprobe"))
    {
        return true;
    }
    if (explicitGpuCapture)
        return false;

    return method == QStringLiteral("cpu") ||
           method == QStringLiteral("gdi") ||
           method == QStringLiteral("x11") ||
           method == QStringLiteral("xorg") ||
           method.startsWith(QStringLiteral("gdi/")) ||
           method.startsWith(QStringLiteral("x11/")) ||
           backend == QStringLiteral("gdi") ||
           backend == QStringLiteral("x11") ||
           backend == QStringLiteral("xorg");
}

bool isCpuEncodePath(const QString &encoderType, const QString &encodePath)
{
    const QString type = encoderType.trimmed().toLower();
    const QString path = encodePath.trimmed();

    if (path.isEmpty() && type.isEmpty())
        return true;

    if (path == QStringLiteral("CpuSoftwareEncode"))
        return true;
    if (path == QStringLiteral("GpuZeroCopyEncode") ||
        path == QStringLiteral("GpuCopyHwEncode") ||
        path == QStringLiteral("CpuUploadHwEncode") ||
        path == QStringLiteral("CpuReadbackHwEncode"))
    {
        return false;
    }

    return type == QStringLiteral("software") ||
           type == QStringLiteral("cpu") ||
           type == QStringLiteral("unknown");
}

} // namespace


int WebRtcCli::effectiveCaptureFps() const
{
    return clampFpsForCurrentPipeline(m_fps);
}


int WebRtcCli::currentPipelineFpsLimit() const
{
    return m_fps > 0 ? m_fps : ConfigUtil->fps;
}


int WebRtcCli::clampFpsForCurrentPipeline(int fps) const
{
    int upper = qBound(1, currentPipelineFpsLimit(), 120);
    if (m_adaptiveVideoFpsCap > 0)
        upper = qMin(upper, m_adaptiveVideoFpsCap);
    if (m_adaptiveCpuFpsCap > 0)
        upper = qMin(upper, m_adaptiveCpuFpsCap);
    return qBound(1, fps, upper);
}


void WebRtcCli::applyEffectiveVideoFpsIfNeeded(const char *reason)
{
    if (m_isOnlyFile || !m_videoTrack)
        return;

    const int effectiveFps = effectiveCaptureFps();
    m_videoTrack->setDesktopTargetResolution(m_visible_width, m_visible_height, effectiveFps);
    LOG_DEBUG("Effective desktop fps applied: requested={}, effective={}, maxLimit={}, networkCap={}, cpuCap={}, cpuLoad={:.1f}, reason={}",
              m_fps,
              effectiveFps,
              currentPipelineFpsLimit(),
              m_adaptiveVideoFpsCap,
              m_adaptiveCpuFpsCap,
              m_lastCpuUsagePercent,
              reason ? reason : "pipeline");
}


void WebRtcCli::startMediaCapture()
{
    if (m_isOnlyFile)
    {
        LOG_DEBUG("Skip media capture start because this is a file-only session");
        return;
    }
    if (m_subscribed)
    {
        LOG_DEBUG("Skip media capture start because media is already subscribed");
        return;
    }

    m_subscribed = true;
    if (m_videoTrack)
    {
        m_videoTrack->setEnabled(true);
    }
    else
    {
        LOG_ERROR("Cannot start desktop capture because video track is missing");
        m_subscribed = false;
        return;
    }

    LOG_INFO("libwebrtc desktop capture started: screen={}, visible={}x{}, fps={}",
             m_screenIndex, m_visible_width, m_visible_height, effectiveCaptureFps());
    notifyCurrentStreamConfig();
    queryMediaStats();
}


void WebRtcCli::stopMediaCapture()
{
    LOG_INFO("Stopping media capture");
    if (m_subscribed)
    {
        m_subscribed = false;
        if (m_videoTrack)
            m_videoTrack->setEnabled(false);
        if (m_mediaStatsTimer)
            m_mediaStatsTimer->stop();
        LOG_INFO("libwebrtc desktop capture stopped");
    }

    m_destroying = true;
    emit destroyCli();
}


void WebRtcCli::checkControlAlive()
{
    if (m_destroying || m_isOnlyFile || !m_connected)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastControlAliveMs <= 0)
        m_lastControlAliveMs = nowMs;

    constexpr qint64 kControlAliveTimeoutMs = 10000;
    if (nowMs - m_lastControlAliveMs > kControlAliveTimeoutMs)
    {
        LOG_WARN("Control side heartbeat timeout ({} ms), stop capture and destroy stale controlled session", nowMs - m_lastControlAliveMs);
        stopMediaCapture();
    }
}


void WebRtcCli::calculateEncodeResolution(int requestedMaxWidth, int requestedMaxHeight)
{
    LOG_DEBUG("Calculating desktop stream constraints - requested max: {}x{}, local screen: {}x{}",
              requestedMaxWidth, requestedMaxHeight, m_screen_width, m_screen_height);

    if (requestedMaxWidth <= 0 || requestedMaxHeight <= 0)
    {
        m_visible_width = m_screen_width;
        m_visible_height = m_screen_height;
        LOG_DEBUG("Using original local visible resolution: {}x{}", m_visible_width, m_visible_height);
    }
    else if (m_screen_width <= requestedMaxWidth && m_screen_height <= requestedMaxHeight)
    {
        m_visible_width = m_screen_width;
        m_visible_height = m_screen_height;
        LOG_DEBUG("Using local visible resolution: {}x{} (fits within requested max)", m_visible_width, m_visible_height);
    }
    else
    {
        const double localAspectRatio = static_cast<double>(m_screen_width) / m_screen_height;
        const double requestedAspectRatio = static_cast<double>(requestedMaxWidth) / requestedMaxHeight;

        if (localAspectRatio > requestedAspectRatio)
        {
            m_visible_width = requestedMaxWidth;
            m_visible_height = static_cast<int>(requestedMaxWidth / localAspectRatio);
        }
        else
        {
            m_visible_height = requestedMaxHeight;
            m_visible_width = static_cast<int>(requestedMaxHeight * localAspectRatio);
        }

        LOG_DEBUG("Scaled to maintain aspect ratio: {}x{} (local aspect: {:.3f}, requested aspect: {:.3f})",
                  m_visible_width, m_visible_height, localAspectRatio, requestedAspectRatio);
    }

    m_visible_width = qMax(2, m_visible_width & ~1);
    m_visible_height = qMax(2, m_visible_height & ~1);
    auto alignUp16 = [](int value) {
        return qMax(16, (value + 15) & ~15);
    };
    m_coded_width = alignUp16(m_visible_width);
    m_coded_height = alignUp16(m_visible_height);

    LOG_DEBUG("Final visible resolution: {}x{}, coded resolution: {}x{}",
              m_visible_width, m_visible_height, m_coded_width, m_coded_height);
}
