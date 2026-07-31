#include "webrtc/cli/webrtc_cli.h"
#include "webrtc/cli/media/webrtc_cli_media_labels.h"

#include "media/codec/airan_video_bitrate_profile.h"

#include <QMetaObject>
#include <QFile>
#include <QPointer>
#include <QStringList>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/processor_info.h>
#endif

namespace
{

bool isHardwareEncoderType(const QString &encoderType)
{
    return encoderType == QStringLiteral("hardware") ||
           encoderType == QStringLiteral("hardware_preferred");
}

bool hasMetric(double value)
{
    return value >= 0.0;
}

bool isCpuReadbackHwEncodePath(const QString &encodePath)
{
    return encodePath.trimmed() == QStringLiteral("CpuReadbackHwEncode");
}

#if defined(Q_OS_WIN)
quint64 fileTimeToTicks(const FILETIME &time)
{
    return (static_cast<quint64>(time.dwHighDateTime) << 32) |
           static_cast<quint64>(time.dwLowDateTime);
}
#endif

bool sampleSystemCpuTicks(quint64 *idleTicks, quint64 *totalTicks)
{
    if (!idleTicks || !totalTicks)
        return false;
#if defined(Q_OS_WIN)
    FILETIME idleTime;
    FILETIME kernelTime;
    FILETIME userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
        return false;
    const quint64 idle = fileTimeToTicks(idleTime);
    const quint64 kernel = fileTimeToTicks(kernelTime);
    const quint64 user = fileTimeToTicks(userTime);
    *idleTicks = idle;
    *totalTicks = kernel + user;
    return *totalTicks > 0;
#elif defined(Q_OS_LINUX)
    QFile statFile(QStringLiteral("/proc/stat"));
    if (!statFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString line = QString::fromLatin1(statFile.readLine()).simplified();
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
#else
    const QStringList fields = line.split(QLatin1Char(' '), QString::SkipEmptyParts);
#endif
    if (fields.size() < 8 || fields.first() != QStringLiteral("cpu"))
        return false;

    quint64 values[10]{};
    const int valueCount = qMin(10, fields.size() - 1);
    for (int index = 0; index < valueCount; ++index)
        values[index] = fields.at(index + 1).toULongLong();

    const quint64 idle = values[3] + values[4];
    quint64 total = 0;
    for (int index = 0; index < valueCount; ++index)
        total += values[index];
    *idleTicks = idle;
    *totalTicks = total;
    return *totalTicks > 0;
#elif defined(Q_OS_MACOS)
    host_cpu_load_info_data_t cpuInfo{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    const kern_return_t result = host_statistics(mach_host_self(),
                                                 HOST_CPU_LOAD_INFO,
                                                 reinterpret_cast<host_info_t>(&cpuInfo),
                                                 &count);
    if (result != KERN_SUCCESS)
        return false;
    const quint64 user = cpuInfo.cpu_ticks[CPU_STATE_USER];
    const quint64 nice = cpuInfo.cpu_ticks[CPU_STATE_NICE];
    const quint64 system = cpuInfo.cpu_ticks[CPU_STATE_SYSTEM];
    const quint64 idle = cpuInfo.cpu_ticks[CPU_STATE_IDLE];
    *idleTicks = idle;
    *totalTicks = user + nice + system + idle;
    return *totalTicks > 0;
#else
    return false;
#endif
}

int nextLowerFpsCap(int currentFps)
{
    if (currentFps > 90)
        return 90;
    if (currentFps > 60)
        return 60;
    if (currentFps > 45)
        return 45;
    if (currentFps > 30)
        return 30;
    if (currentFps > 20)
        return 20;
    if (currentFps > 15)
        return 15;
    return 10;
}

int nextHigherFpsCap(int currentCap, int maxFps)
{
    if (currentCap <= 0)
        return 0;
    if (currentCap < 15)
        return qMin(15, maxFps);
    if (currentCap < 20)
        return qMin(20, maxFps);
    if (currentCap < 30)
        return qMin(30, maxFps);
    if (currentCap < 45)
        return qMin(45, maxFps);
    if (currentCap < 60)
        return qMin(60, maxFps);
    if (currentCap < 90)
        return qMin(90, maxFps);
    if (currentCap < maxFps)
        return maxFps;
    return 0;
}

airan::media::DesktopVideoBitrateLimits desktopLimitsFor(const QString &qualityProfile,
                                                         int width,
                                                         int height,
                                                         int fps)
{
    return airan::media::desktopVideoBitrateLimits(
        width,
        height,
        fps,
        airan::media::desktopVideoQualityProfileFromName(qualityProfile.toStdString()));
}

int bestFpsCapForBitrateBudget(int width,
                               int height,
                               int maxFps,
                               double bitrateBudgetBps,
                               const QString &qualityProfile,
                               bool preferStartBitrate)
{
    if (!hasMetric(bitrateBudgetBps) || bitrateBudgetBps <= 0.0)
        return 0;

    constexpr double kJitterReserve = 0.85;
    const double usableBudget = bitrateBudgetBps * kJitterReserve;
    const int candidates[] = {120, 90, 60, 45, 30, 20, 15, 10, 5, 1};
    for (const int candidate : candidates)
    {
        if (candidate > maxFps)
            continue;
        const auto limits = desktopLimitsFor(qualityProfile, width, height, candidate);
        const double requiredBps = static_cast<double>(preferStartBitrate ? limits.start_bps : limits.min_bps);
        if (requiredBps <= usableBudget)
            return candidate >= maxFps ? 0 : candidate;
    }
    return 1;
}

QString inferredActualEncodePath(const QString &encoderImplementation,
                                 const QString &encoderType,
                                 const QString &capturePath,
                                 const QString &captureBackend,
                                 const QString &captureMethod,
                                 const QString &previousEncodePath)
{
    if (encoderImplementation.isEmpty())
        return previousEncodePath;
    if (!isHardwareEncoderType(encoderType))
        return QStringLiteral("CpuSoftwareEncode");

    const QString normalizedCapturePath = capturePath.trimmed();
    const QString normalizedCaptureBackend = captureBackend.trimmed().toLower();
    const QString normalizedCaptureMethod = captureMethod.trimmed().toLower();
    const QString normalizedPrevious = previousEncodePath.trimmed();
    const QString encoderBackend = webrtc_cli_internal::encoderBackendFromImplementation(encoderImplementation);
    const bool nativeGpuBackend =
        normalizedCaptureBackend == QStringLiteral("wgc") ||
        normalizedCaptureBackend == QStringLiteral("dxgi") ||
        normalizedCaptureMethod == QStringLiteral("wgc") ||
        normalizedCaptureMethod == QStringLiteral("dxgi");
    const bool nativeGpuCapture =
        normalizedCapturePath == QStringLiteral("NativeGpuCapture") || nativeGpuBackend;
    if (nativeGpuCapture && encoderBackend == QStringLiteral("qsv"))
        return QStringLiteral("CpuReadbackHwEncode");
    if (normalizedCapturePath == QStringLiteral("NativeGpuCapture"))
    {
        if (normalizedPrevious == QStringLiteral("GpuZeroCopyEncode") ||
            normalizedPrevious == QStringLiteral("GpuCopyHwEncode") ||
            normalizedPrevious == QStringLiteral("CpuReadbackHwEncode"))
            return normalizedPrevious;
        return QStringLiteral("GpuCopyHwEncode");
    }
    if (nativeGpuBackend)
    {
        if (normalizedPrevious == QStringLiteral("GpuZeroCopyEncode") ||
            normalizedPrevious == QStringLiteral("GpuCopyHwEncode"))
            return normalizedPrevious;
        return QStringLiteral("GpuCopyHwEncode");
    }
    if (normalizedCapturePath == QStringLiteral("WebRtcDerivedCpuCapture"))
        return QStringLiteral("CpuUploadHwEncode");
    if (normalizedPrevious == QStringLiteral("GpuZeroCopyEncode") ||
        normalizedPrevious == QStringLiteral("GpuCopyHwEncode") ||
        normalizedPrevious == QStringLiteral("CpuReadbackHwEncode") ||
        normalizedPrevious == QStringLiteral("CpuUploadHwEncode"))
        return normalizedPrevious;
    return QStringLiteral("CpuUploadHwEncode");
}

} // namespace


void WebRtcCli::queryMediaStats()
{
    if (m_isOnlyFile || !m_peerConnection || m_destroying)
        return;

    QPointer<WebRtcCli> self(this);
    const auto callbackLifetime = m_callbackLifetime;
    m_peerConnection->queryMediaStats([self, callbackLifetime](rtc::MediaStats stats) {
        auto permit = callbackLifetime->tryEnter();
        if (!permit)
            return;
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), "applyMediaStats", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(stats.videoCodec).trimmed()),
                                  Q_ARG(QString, QString::fromStdString(stats.encoderImplementation).trimmed()),
                                  Q_ARG(double, stats.availableOutgoingBitrateBps),
                                  Q_ARG(double, stats.targetBitrateBps),
                                  Q_ARG(double, stats.fractionLost),
                                  Q_ARG(double, stats.rttMs),
                                  Q_ARG(QString, QString::fromStdString(stats.qualityLimitationReason).trimmed()));
    });
}


void WebRtcCli::applyMediaStats(const QString &codec,
                                const QString &encoderImplementation,
                                double availableOutgoingBitrateBps,
                                double targetBitrateBps,
                                double fractionLost,
                                double rttMs,
                                const QString &qualityLimitationReason)
{
    if (m_destroying)
        return;

    bool changed = false;
    if (!codec.isEmpty() && codec != m_negotiatedVideoCodec)
    {
        m_negotiatedVideoCodec = codec;
        changed = true;
    }

    if (!encoderImplementation.isEmpty())
    {
        const QString encoderName = webrtc_cli_internal::readableEncoderName(encoderImplementation, m_negotiatedVideoCodec);
        const QString encoderType = webrtc_cli_internal::encoderTypeFromImplementation(encoderImplementation);
        if (encoderName != m_currentEncoderName || encoderType != m_currentEncoderType)
        {
            m_currentEncoderName = encoderName;
            m_currentEncoderType = encoderType;
            changed = true;
            LOG_INFO("Outbound video encoder implementation: raw={}, label={}, type={}",
                     encoderImplementation, encoderName, encoderType);
        }
        const QString encodePath =
            inferredActualEncodePath(encoderImplementation,
                                     encoderType,
                                     m_currentCapturePath,
                                     m_currentCaptureBackend,
                                     m_currentCaptureMethod,
                                     m_currentEncodePath);
        if (!encodePath.isEmpty() && encodePath != m_currentEncodePath)
        {
            m_currentEncodePath = encodePath;
            changed = true;
            LOG_INFO("Outbound video encode path inferred from encoder stats: implementation={}, capturePath={}, encodePath={}",
                     encoderImplementation, m_currentCapturePath, m_currentEncodePath);
        }
    }

    if (m_desktopLocked)
    {
        if (changed)
            notifyCurrentStreamConfig();
        LOG_DEBUG("Media adaptation skipped while Windows session is locked");
        return;
    }
    if (m_captureRecoveryHoldUntilMs > 0 &&
        QDateTime::currentMSecsSinceEpoch() < m_captureRecoveryHoldUntilMs)
    {
        if (m_adaptiveVideoFpsCap != 0 || m_adaptiveCpuFpsCap != 0)
        {
            m_adaptiveVideoFpsCap = 0;
            m_adaptiveFpsPoorScore = 0;
            m_adaptiveFpsGoodScore = 0;
            m_adaptiveCpuFpsCap = 0;
            m_adaptiveCpuPoorScore = 0;
            m_adaptiveCpuGoodScore = 0;
            applyEffectiveVideoFpsIfNeeded("capture-recovery-hold");
            notifyCurrentStreamConfig();
        }
        if (changed)
            notifyCurrentStreamConfig();
        LOG_DEBUG("Media adaptation skipped during capture recovery hold");
        return;
    }

    updateAutomaticQualityProfile(availableOutgoingBitrateBps,
                                  targetBitrateBps,
                                  fractionLost,
                                  rttMs,
                                  qualityLimitationReason);
    updateAdaptiveVideoFpsCap(availableOutgoingBitrateBps,
                              targetBitrateBps,
                              fractionLost,
                              rttMs,
                              qualityLimitationReason);
    updateAdaptiveCpuFpsCap(fractionLost,
                            rttMs,
                            qualityLimitationReason);

    if (changed)
    {
        applyEffectiveVideoFpsIfNeeded("encoder-stats");
        notifyCurrentStreamConfig();
    }
}

void WebRtcCli::updateAdaptiveCpuFpsCap(double fractionLost,
                                        double rttMs,
                                        const QString &qualityLimitationReason)
{
    if (!m_videoTrack)
        return;

    quint64 idleTicks = 0;
    quint64 totalTicks = 0;
    if (!sampleSystemCpuTicks(&idleTicks, &totalTicks))
        return;

    if (m_lastCpuTotalTicks == 0 || totalTicks <= m_lastCpuTotalTicks || idleTicks < m_lastCpuIdleTicks)
    {
        m_lastCpuIdleTicks = idleTicks;
        m_lastCpuTotalTicks = totalTicks;
        return;
    }

    const quint64 idleDelta = idleTicks - m_lastCpuIdleTicks;
    const quint64 totalDelta = totalTicks - m_lastCpuTotalTicks;
    m_lastCpuIdleTicks = idleTicks;
    m_lastCpuTotalTicks = totalTicks;
    if (totalDelta == 0)
        return;

    m_lastCpuUsagePercent = 100.0 * static_cast<double>(totalDelta - qMin(idleDelta, totalDelta)) /
                            static_cast<double>(totalDelta);

    const int maxFps = qBound(1, currentPipelineFpsLimit(), 120);
    const int currentEffectiveFps = effectiveCaptureFps();
    const QString limitation = qualityLimitationReason.trimmed().toLower();
    const bool networkPoor =
        limitation == QStringLiteral("bandwidth") ||
        (hasMetric(fractionLost) && fractionLost >= 0.03) ||
        (hasMetric(rttMs) && rttMs >= 350.0);
    const bool cpuHigh = m_lastCpuUsagePercent >= 50.0;
    const bool cpuComfortable = m_lastCpuUsagePercent >= 0.0 && m_lastCpuUsagePercent < 40.0;

    int nextCap = m_adaptiveCpuFpsCap;
    if (cpuHigh)
    {
        ++m_adaptiveCpuPoorScore;
        m_adaptiveCpuGoodScore = 0;
        if (m_adaptiveCpuPoorScore >= 3)
            nextCap = nextLowerFpsCap(currentEffectiveFps);
    }
    else if (cpuComfortable && !networkPoor)
    {
        m_adaptiveCpuPoorScore = qMax(0, m_adaptiveCpuPoorScore - 1);
        ++m_adaptiveCpuGoodScore;
        if (m_adaptiveCpuGoodScore >= 8)
            nextCap = nextHigherFpsCap(m_adaptiveCpuFpsCap, maxFps);
    }
    else
    {
        m_adaptiveCpuPoorScore = qMax(0, m_adaptiveCpuPoorScore - 1);
        m_adaptiveCpuGoodScore = qMax(0, m_adaptiveCpuGoodScore - 1);
    }

    if (nextCap > 0)
        nextCap = qBound(1, nextCap, maxFps);
    if (nextCap >= maxFps)
        nextCap = 0;
    if (nextCap == m_adaptiveCpuFpsCap)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool loweringFps = nextCap > 0 && (m_adaptiveCpuFpsCap == 0 || nextCap < m_adaptiveCpuFpsCap);
    const qint64 minSwitchIntervalMs = loweringFps ? 5000 : 15000;
    if (m_lastAdaptiveCpuFpsSwitchMs > 0 && nowMs - m_lastAdaptiveCpuFpsSwitchMs < minSwitchIntervalMs)
        return;

    const int previousCap = m_adaptiveCpuFpsCap;
    m_adaptiveCpuFpsCap = nextCap;
    m_lastAdaptiveCpuFpsSwitchMs = nowMs;
    m_adaptiveCpuPoorScore = 0;
    m_adaptiveCpuGoodScore = 0;
    applyEffectiveVideoFpsIfNeeded("adaptive-cpu-fps");
    LOG_INFO("Adaptive CPU fps cap changed: {} -> {}, effective={}, cpuLoad={:.1f}, loss={}, rtt={}ms, limitation={}",
             previousCap,
             m_adaptiveCpuFpsCap,
             effectiveCaptureFps(),
             m_lastCpuUsagePercent,
             fractionLost,
             rttMs,
             qualityLimitationReason);
    notifyCurrentStreamConfig();
}

void WebRtcCli::updateAutomaticQualityProfile(double availableOutgoingBitrateBps,
                                              double targetBitrateBps,
                                              double fractionLost,
                                              double rttMs,
                                              const QString &qualityLimitationReason)
{
    if (!m_autoQualityProfile || !m_videoTrack)
        return;

    const bool bandwidthLimited = qualityLimitationReason.trimmed().toLower() == QStringLiteral("bandwidth");
    const bool atWeakProfile = m_qualityProfile == QStringLiteral("weak_clear");
    const bool atBalancedProfile = m_qualityProfile == QStringLiteral("balanced");
    const bool atLanProfile = m_qualityProfile == QStringLiteral("lan_hd");
    const int intendedFps = qBound(1, qMin(qMax(1, m_fps), qBound(1, currentPipelineFpsLimit(), 120)), 120);
    const auto weakClearLimits = desktopLimitsFor(QStringLiteral("weak_clear"),
                                                  m_visible_width,
                                                  m_visible_height,
                                                  intendedFps);
    const auto lanLimits = desktopLimitsFor(QStringLiteral("lan_hd"),
                                            m_visible_width,
                                            m_visible_height,
                                            intendedFps);
    const double clearMinBps = static_cast<double>(weakClearLimits.min_bps);
    const double clearStartBps = static_cast<double>(weakClearLimits.start_bps);
    const double lanMinBps = static_cast<double>(lanLimits.min_bps);
    const double lanStartBps = static_cast<double>(lanLimits.start_bps);
    const bool lowLoss =
        !hasMetric(fractionLost) || fractionLost < 0.02;
    const bool veryLowLoss =
        !hasMetric(fractionLost) || fractionLost < 0.01;
    const bool usableRtt =
        !hasMetric(rttMs) || rttMs < 300.0;
    const bool excellentRtt =
        !hasMetric(rttMs) || rttMs < 200.0;
    const bool capacityTooLow =
        bandwidthLimited &&
        ((hasMetric(availableOutgoingBitrateBps) && availableOutgoingBitrateBps < clearMinBps * 0.55) ||
         (hasMetric(targetBitrateBps) && targetBitrateBps < clearMinBps * 0.45));
    const bool capacityModeratePoor =
        bandwidthLimited &&
        ((hasMetric(availableOutgoingBitrateBps) && availableOutgoingBitrateBps < clearMinBps) ||
         (hasMetric(targetBitrateBps) && targetBitrateBps < clearMinBps * 0.80));
    const bool veryPoor =
        capacityTooLow ||
        (hasMetric(fractionLost) && fractionLost >= 0.08) ||
        (hasMetric(rttMs) && rttMs >= 650.0);
    const bool moderatePoor =
        capacityModeratePoor ||
        (hasMetric(fractionLost) && fractionLost >= 0.03) ||
        (hasMetric(rttMs) && rttMs >= 350.0);
    const bool stableGood =
        lowLoss &&
        usableRtt &&
        !bandwidthLimited;
    double bitrateBudgetBps = -1.0;
    if (hasMetric(availableOutgoingBitrateBps) && hasMetric(targetBitrateBps))
        bitrateBudgetBps = qMin(availableOutgoingBitrateBps, targetBitrateBps);
    else if (hasMetric(availableOutgoingBitrateBps))
        bitrateBudgetBps = availableOutgoingBitrateBps;
    else if (hasMetric(targetBitrateBps))
        bitrateBudgetBps = targetBitrateBps;
    const double usableBudgetBps = hasMetric(bitrateBudgetBps) ? bitrateBudgetBps * 0.85 : -1.0;
    const bool lanCapacity =
        (!hasMetric(usableBudgetBps) || usableBudgetBps >= lanMinBps) &&
        (!hasMetric(targetBitrateBps) || targetBitrateBps >= clearStartBps);
    const bool excellent =
        (!hasMetric(usableBudgetBps) || usableBudgetBps >= lanStartBps) &&
        (!hasMetric(targetBitrateBps) || targetBitrateBps >= clearStartBps) &&
        veryLowLoss &&
        excellentRtt &&
        !bandwidthLimited;
    const bool cpuHasRoom =
        !hasMetric(m_lastCpuUsagePercent) ||
        m_lastCpuUsagePercent < 50.0 ||
        m_adaptiveCpuFpsCap == 0;

    QString nextProfile;
    if (veryPoor)
    {
        ++m_qualityPoorScore;
        m_qualityGoodScore = 0;
        if (m_qualityPoorScore >= 3 && atLanProfile)
            nextProfile = QStringLiteral("weak_clear");
        else if (m_qualityPoorScore >= 5 && atWeakProfile)
            nextProfile = QStringLiteral("balanced");
    }
    else if (moderatePoor)
    {
        ++m_qualityPoorScore;
        m_qualityGoodScore = 0;
        if (m_qualityPoorScore >= 4 && atLanProfile)
            nextProfile = QStringLiteral("weak_clear");
    }
    else if (stableGood)
    {
        ++m_qualityGoodScore;
        m_qualityPoorScore = 0;
        if (atBalancedProfile && m_qualityGoodScore >= 5)
            nextProfile = QStringLiteral("weak_clear");
        else if ((atWeakProfile || atBalancedProfile) &&
                 lanCapacity &&
                 excellent &&
                 cpuHasRoom &&
                 m_qualityGoodScore >= 5)
        {
            nextProfile = QStringLiteral("lan_hd");
        }
    }
    else
    {
        m_qualityPoorScore = qMax(0, m_qualityPoorScore - 1);
        m_qualityGoodScore = qMax(0, m_qualityGoodScore - 1);
    }

    if (nextProfile.isEmpty() || nextProfile == m_qualityProfile)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastQualityProfileSwitchMs > 0 && nowMs - m_lastQualityProfileSwitchMs < 30000)
        return;

    const QString previousProfile = m_qualityProfile;
    m_qualityProfile = nextProfile;
    m_lastQualityProfileSwitchMs = nowMs;
    m_qualityPoorScore = 0;
    m_qualityGoodScore = 0;
    m_videoTrack->setDesktopQualityProfile(m_qualityProfile.toStdString());
    LOG_INFO("Automatic video quality profile switched: {} -> {}, availableOutgoing={}bps, target={}bps, loss={}, rtt={}ms, limitation={}",
             previousProfile,
             m_qualityProfile,
             availableOutgoingBitrateBps,
             targetBitrateBps,
             fractionLost,
             rttMs,
             qualityLimitationReason);
    notifyCurrentStreamConfig();
}

void WebRtcCli::updateAdaptiveVideoFpsCap(double availableOutgoingBitrateBps,
                                          double targetBitrateBps,
                                          double fractionLost,
                                          double rttMs,
                                          const QString &qualityLimitationReason)
{
    if (!m_videoTrack)
        return;

    const QString limitation = qualityLimitationReason.trimmed().toLower();
    const bool bandwidthLimited = limitation == QStringLiteral("bandwidth");
    const bool severeLoss = hasMetric(fractionLost) && fractionLost >= 0.08;
    const bool poorLoss = hasMetric(fractionLost) && fractionLost >= 0.03;
    const bool severeRtt = hasMetric(rttMs) && rttMs >= 650.0;
    const bool poorRtt = hasMetric(rttMs) && rttMs >= 350.0;
    const bool hasTrustedBitrateTarget = hasMetric(targetBitrateBps);
    const bool hasCongestionSignal = bandwidthLimited || severeLoss || poorLoss || severeRtt || poorRtt;
    const int maxFps = qBound(1, qMin(qMax(1, m_fps), qBound(1, currentPipelineFpsLimit(), 120)), 120);
    const auto clearLimits = desktopLimitsFor(QStringLiteral("weak_clear"),
                                              m_visible_width,
                                              m_visible_height,
                                              maxFps);
    const double clearMinBps = static_cast<double>(clearLimits.min_bps);
    const double clearStartBps = static_cast<double>(clearLimits.start_bps);
    double bitrateBudgetBps = -1.0;
    if (hasMetric(availableOutgoingBitrateBps) && hasMetric(targetBitrateBps))
        bitrateBudgetBps = qMin(availableOutgoingBitrateBps, targetBitrateBps);
    else if (hasMetric(availableOutgoingBitrateBps))
        bitrateBudgetBps = availableOutgoingBitrateBps;
    else if (hasMetric(targetBitrateBps))
        bitrateBudgetBps = targetBitrateBps;

    const bool severeCapacity =
        bandwidthLimited &&
        ((hasTrustedBitrateTarget && hasMetric(availableOutgoingBitrateBps) && availableOutgoingBitrateBps < clearMinBps * 0.45) ||
         (hasMetric(targetBitrateBps) && targetBitrateBps < clearMinBps * 0.35));
    const bool poorCapacity =
        bandwidthLimited &&
        ((hasTrustedBitrateTarget && hasMetric(availableOutgoingBitrateBps) && availableOutgoingBitrateBps < clearMinBps * 0.70) ||
         (hasMetric(targetBitrateBps) && targetBitrateBps < clearMinBps * 0.55));
    const bool moderateCapacity =
        bandwidthLimited &&
        ((hasTrustedBitrateTarget && hasMetric(availableOutgoingBitrateBps) && availableOutgoingBitrateBps < clearMinBps) ||
         (hasMetric(targetBitrateBps) && targetBitrateBps < clearMinBps * 0.80));
    const bool preserveReadbackFps =
        isCpuReadbackHwEncodePath(m_currentEncodePath) &&
        !(severeLoss || severeRtt || severeCapacity || poorLoss || poorRtt);

    int nextCap = 0;
    if (!hasTrustedBitrateTarget && !hasCongestionSignal)
    {
        nextCap = m_adaptiveVideoFpsCap;
    }
    else if (severeCapacity || severeLoss || severeRtt)
    {
        nextCap = bestFpsCapForBitrateBudget(m_visible_width,
                                             m_visible_height,
                                             maxFps,
                                             bitrateBudgetBps,
                                             QStringLiteral("weak_clear"),
                                             false);
        if ((severeLoss || severeRtt) && nextCap == 0)
            nextCap = qMin(30, maxFps);
    }
    else if (poorCapacity)
    {
        nextCap = bestFpsCapForBitrateBudget(m_visible_width,
                                             m_visible_height,
                                             maxFps,
                                             bitrateBudgetBps,
                                             QStringLiteral("weak_clear"),
                                             false);
    }
    else if (poorLoss || poorRtt)
    {
        nextCap = m_adaptiveVideoFpsCap;
    }
    else if (moderateCapacity)
    {
        nextCap = bestFpsCapForBitrateBudget(m_visible_width,
                                             m_visible_height,
                                             maxFps,
                                             bitrateBudgetBps,
                                             QStringLiteral("weak_clear"),
                                             true);
    }

    if (preserveReadbackFps)
        nextCap = 0;

    const bool lossOnlyWithHealthyRtt = !bandwidthLimited && !severeRtt && !poorRtt &&
                                       (severeLoss || poorLoss);
    if (nextCap > 0 && lossOnlyWithHealthyRtt)
        nextCap = qMax(nextCap, qMin(10, maxFps));

    const bool stableGood =
        !bandwidthLimited &&
        (!hasMetric(availableOutgoingBitrateBps) || availableOutgoingBitrateBps >= clearStartBps) &&
        (!hasMetric(targetBitrateBps) || targetBitrateBps >= clearMinBps) &&
        (!hasMetric(fractionLost) || fractionLost < 0.01) &&
        (!hasMetric(rttMs) || rttMs < 180.0);

    bool recoveringFps = false;
    if (stableGood && m_adaptiveVideoFpsCap > 0 && nextCap == 0)
    {
        nextCap = nextHigherFpsCap(m_adaptiveVideoFpsCap, maxFps);
        recoveringFps = true;
    }

    const bool unrestricted = nextCap == 0;
    if (unrestricted && m_adaptiveVideoFpsCap > 0 && !stableGood && !preserveReadbackFps)
    {
        nextCap = m_adaptiveVideoFpsCap;
    }

    if (preserveReadbackFps)
    {
        m_adaptiveFpsPoorScore = 0;
        m_adaptiveFpsGoodScore = 15;
    }
    else if (recoveringFps)
    {
        m_adaptiveFpsPoorScore = qMax(0, m_adaptiveFpsPoorScore - 1);
        ++m_adaptiveFpsGoodScore;
        if (m_adaptiveFpsGoodScore < 5)
            nextCap = m_adaptiveVideoFpsCap;
    }
    else if (nextCap > 0)
    {
        ++m_adaptiveFpsPoorScore;
        m_adaptiveFpsGoodScore = 0;
        if (m_adaptiveFpsPoorScore < 5 && m_adaptiveVideoFpsCap != 0)
            nextCap = m_adaptiveVideoFpsCap;
    }
    else
    {
        m_adaptiveFpsPoorScore = qMax(0, m_adaptiveFpsPoorScore - 1);
        ++m_adaptiveFpsGoodScore;
        if (m_adaptiveFpsGoodScore < 15)
            nextCap = m_adaptiveVideoFpsCap;
    }

    if (nextCap == m_adaptiveVideoFpsCap)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool loweringFps = nextCap > 0 && (m_adaptiveVideoFpsCap == 0 || nextCap < m_adaptiveVideoFpsCap);
    const qint64 minSwitchIntervalMs = loweringFps ? 5000 : 20000;
    if (m_lastAdaptiveFpsSwitchMs > 0 && nowMs - m_lastAdaptiveFpsSwitchMs < minSwitchIntervalMs)
        return;

    const int previousCap = m_adaptiveVideoFpsCap;
    m_adaptiveVideoFpsCap = nextCap;
    m_lastAdaptiveFpsSwitchMs = nowMs;
    m_adaptiveFpsPoorScore = 0;
    m_adaptiveFpsGoodScore = 0;
    applyEffectiveVideoFpsIfNeeded("adaptive-network-fps");
    m_videoTrack->requestKeyFrame();
    LOG_INFO("Adaptive video fps cap changed: {} -> {}, effective={}, availableOutgoing={}bps, target={}bps, loss={}, rtt={}ms, limitation={}",
             previousCap,
             m_adaptiveVideoFpsCap,
             effectiveCaptureFps(),
             availableOutgoingBitrateBps,
             targetBitrateBps,
             fractionLost,
             rttMs,
             qualityLimitationReason);
    notifyCurrentStreamConfig();
}
