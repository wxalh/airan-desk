#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"

#if defined(_WIN32)
#include "desktop_capture/win/screen_capture_utils.h"
#if defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include "desktop_capture/win/dxgi_duplicator_controller.h"
#include "desktop_capture/win/screen_capturer_win_directx.h"
#endif
#endif
#if defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
#include "desktop_capture/mac/desktop_configuration.h"
#endif
#if defined(WEBRTC_USE_PIPEWIRE) || defined(WEBRTC_USE_X11)
#include "desktop_capture/desktop_capture_options.h"
#include "desktop_capture/desktop_capturer.h"
#endif
#if defined(WEBRTC_USE_X11)
#include "desktop_capture/linux/x11/screen_capturer_x11.h"
#endif

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QScreen>
#include <QVector>

#include <cstdint>
#include <memory>
#include <optional>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <vector>

namespace
{
constexpr int kStreamConfigApplyDelayMs = 3000;
constexpr int kMinAdaptiveFps = 1;

struct ScreenCatalogEntry
{
    QString id;
    int index = -1;
    intptr_t sourceId = 0;
    bool hasSourceId = false;
    bool primary = false;
    QRect rect;
    QString name;
};

int boundedRemoteFpsSetting(const QJsonObject &object, const QString &key, int fallback)
{
    return qBound(1, JsonUtil::getInt(object, key, fallback), 120);
}

#if defined(_WIN32)
bool hasInvalidWindowsSourceRect(const airan::desktop_capture::DesktopCapturer::SourceList &sources)
{
    if (sources.empty())
        return true;
    for (const auto &source : sources)
    {
        if (airan::desktop_capture::GetScreenRect(source.id, std::nullopt).is_empty())
            return true;
    }
    return false;
}

bool buildExpectedWindowsSourceList(airan::desktop_capture::DesktopCapturer::SourceList *sources,
                                    std::vector<std::string> *deviceNames)
{
    if (!sources || !deviceNames)
        return false;
    sources->clear();
    deviceNames->clear();

#if defined(RTC_ENABLE_WIN_WGC)
    if (ConfigUtil->enable_wgc_capture)
        return airan::desktop_capture::GetScreenList(sources, deviceNames);
#endif

#if defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (ConfigUtil->enable_dxgi_capture && airan::desktop_capture::ScreenCapturerWinDirectx::IsSupported())
    {
        std::vector<std::string> dxgiDeviceNames;
        if (airan::desktop_capture::DxgiDuplicatorController::Instance()->GetDeviceNames(&dxgiDeviceNames) &&
            airan::desktop_capture::ScreenCapturerWinDirectx::GetScreenListFromDeviceNames(dxgiDeviceNames, sources))
        {
            *deviceNames = dxgiDeviceNames;
            return true;
        }
    }
#endif

    airan::desktop_capture::DesktopCapturer::SourceList deviceSources;
    std::vector<std::string> deviceSourceNames;
    const bool deviceResult = airan::desktop_capture::GetScreenList(&deviceSources, &deviceSourceNames);
    airan::desktop_capture::DesktopCapturer::SourceList monitorSources;
    const bool monitorResult = airan::desktop_capture::GetAiranMonitorList(&monitorSources);
    const bool deviceRectInvalid = hasInvalidWindowsSourceRect(deviceSources);
    if (monitorResult &&
        (monitorSources.size() > deviceSources.size() ||
         !deviceResult || deviceSources.empty() || deviceRectInvalid))
    {
        LOG_INFO("Windows desktop source list uses monitor sources: monitors={}, displayDevices={}, deviceRectInvalid={}",
                 monitorSources.size(), deviceSources.size(), deviceRectInvalid);
        *sources = monitorSources;
        deviceNames->clear();
        return true;
    }

    *sources = deviceSources;
    *deviceNames = deviceSourceNames;
    return deviceResult;
}

QRect desktopScreenPhysicalRect(int screenIndex)
{
    airan::desktop_capture::DesktopCapturer::SourceList sources;
    std::vector<std::string> deviceNames;
    if (buildExpectedWindowsSourceList(&sources, &deviceNames) &&
        screenIndex >= 0 &&
        screenIndex < static_cast<int>(sources.size()))
    {
        const auto rect = airan::desktop_capture::GetScreenRect(sources[static_cast<size_t>(screenIndex)].id, std::nullopt);
        if (!rect.is_empty())
            return QRect(rect.left(), rect.top(), qMax(1, rect.width()), qMax(1, rect.height()));
    }

    QScreen *screen = QGuiApplication::screens().value(screenIndex, QGuiApplication::primaryScreen());
    if (!screen)
        return QRect(0, 0, 1920, 1080);

    const QRect geometry = screen->geometry();
    const qreal dpr = screen->devicePixelRatio();
    return QRect(qRound(geometry.x() * dpr),
                 qRound(geometry.y() * dpr),
                 qMax(1, qRound(geometry.width() * dpr)),
                 qMax(1, qRound(geometry.height() * dpr)));
}
#else
QRect desktopScreenPhysicalRect(int screenIndex)
{
    QScreen *screen = QGuiApplication::screens().value(screenIndex, QGuiApplication::primaryScreen());
    if (!screen)
        return QRect(0, 0, 1920, 1080);
    const QRect geometry = screen->geometry();
    const qreal dpr = screen->devicePixelRatio();
    return QRect(qRound(geometry.x() * dpr),
                 qRound(geometry.y() * dpr),
                 qMax(1, qRound(geometry.width() * dpr)),
                 qMax(1, qRound(geometry.height() * dpr)));
}
#endif

QString screenIdForCatalogIndex(QScreen *screen, int screenIndex, bool primary)
{
    (void)primary;
    (void)screen;
    return QStringLiteral("screen-%1").arg(screenIndex);
}

QVector<ScreenCatalogEntry> buildScreenCatalogSnapshot()
{
    QVector<ScreenCatalogEntry> catalog;
#if defined(_WIN32)
    airan::desktop_capture::DesktopCapturer::SourceList sources;
    std::vector<std::string> deviceNames;
    if (buildExpectedWindowsSourceList(&sources, &deviceNames) && !sources.empty())
    {
        for (int i = 0; i < static_cast<int>(sources.size()); ++i)
        {
            const auto rect = airan::desktop_capture::GetScreenRect(sources[static_cast<size_t>(i)].id, std::nullopt);
            if (rect.is_empty())
                continue;

            ScreenCatalogEntry entry;
            entry.id = QStringLiteral("win-source-%1").arg(i);
            entry.index = i;
            entry.primary = rect.left() == 0 && rect.top() == 0;
            entry.rect = QRect(rect.left(), rect.top(), qMax(1, rect.width()), qMax(1, rect.height()));
            if (i < static_cast<int>(deviceNames.size()))
                entry.name = QString::fromStdString(deviceNames[static_cast<size_t>(i)]);
            catalog.append(entry);
        }
        if (!catalog.isEmpty())
            return catalog;
    }
#endif
#if defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
    const airan::desktop_capture::MacDesktopConfiguration macConfig =
        airan::desktop_capture::MacDesktopConfiguration::GetCurrent(
            airan::desktop_capture::MacDesktopConfiguration::TopLeftOrigin);
    for (int i = 0; i < static_cast<int>(macConfig.displays.size()); ++i)
    {
        const auto &display = macConfig.displays[static_cast<size_t>(i)];
        if (display.pixel_bounds.is_empty())
            continue;

        ScreenCatalogEntry entry;
        entry.id = QStringLiteral("mac-display-%1").arg(static_cast<qulonglong>(display.id));
        entry.index = i;
        entry.sourceId = static_cast<intptr_t>(display.id);
        entry.hasSourceId = true;
        entry.primary = i == 0;
        entry.rect = QRect(display.pixel_bounds.left(),
                           display.pixel_bounds.top(),
                           qMax(1, display.pixel_bounds.width()),
                           qMax(1, display.pixel_bounds.height()));
        entry.name = display.is_builtin
                         ? QStringLiteral("Built-in Display")
                         : QStringLiteral("Display %1").arg(i + 1);
        catalog.append(entry);
    }
    if (!catalog.isEmpty())
        return catalog;
#endif
#if defined(WEBRTC_USE_PIPEWIRE)
    if (airan::desktop_capture::DesktopCapturer::IsRunningUnderWayland())
    {
        const QRect rect = desktopScreenPhysicalRect(0);
        ScreenCatalogEntry entry;
        entry.id = QStringLiteral("pipewire-source-0");
        entry.index = 0;
        entry.primary = true;
        entry.rect = rect.isValid() ? rect : QRect(0, 0, 1920, 1080);
        entry.name = QStringLiteral("PipeWire Screen");
        catalog.append(entry);
        return catalog;
    }
#endif
#if defined(WEBRTC_USE_X11)
    if (!airan::desktop_capture::DesktopCapturer::IsRunningUnderWayland())
    {
        airan::desktop_capture::DesktopCaptureOptions options =
            airan::desktop_capture::DesktopCaptureOptions::CreateDefault();
        std::unique_ptr<airan::desktop_capture::DesktopCapturer> capturer =
            airan::desktop_capture::ScreenCapturerX11::CreateRawScreenCapturer(options);
        airan::desktop_capture::DesktopCapturer::SourceList sources;
        if (capturer && capturer->GetSourceList(&sources) && !sources.empty())
        {
            const QList<QScreen *> screens = QGuiApplication::screens();
            QScreen *primary = QGuiApplication::primaryScreen();
            for (int i = 0; i < static_cast<int>(sources.size()); ++i)
            {
                QScreen *screen = screens.value(i, nullptr);
                const QRect rect = screen ? desktopScreenPhysicalRect(i) : QRect(0, 0, 1920, 1080);

                ScreenCatalogEntry entry;
                entry.id = QStringLiteral("x11-source-%1").arg(i);
                entry.index = i;
                entry.sourceId = static_cast<intptr_t>(sources[static_cast<size_t>(i)].id);
                entry.hasSourceId = true;
                entry.primary = screen && primary && screen == primary;
                entry.rect = rect.isValid() ? rect : QRect(0, 0, 1920, 1080);
                entry.name = !sources[static_cast<size_t>(i)].title.empty()
                                 ? QString::fromStdString(sources[static_cast<size_t>(i)].title)
                                 : QStringLiteral("X11 Screen %1").arg(i + 1);
                catalog.append(entry);
            }
            if (!catalog.isEmpty())
                return catalog;
        }
    }
#endif

    const QList<QScreen *> screens = QGuiApplication::screens();
    QScreen *primary = QGuiApplication::primaryScreen();
    for (int i = 0; i < screens.size(); ++i)
    {
        QScreen *screen = screens.at(i);
        if (!screen)
            continue;
        const bool primaryScreen = primary && screen == primary;
        const QRect rect = desktopScreenPhysicalRect(i);

        ScreenCatalogEntry entry;
        entry.id = screenIdForCatalogIndex(screen, i, primaryScreen);
        entry.index = i;
        entry.primary = primaryScreen;
        entry.rect = rect.isValid() ? rect : QRect(0, 0, 1920, 1080);
        entry.name = screen->name();
        catalog.append(entry);
    }

    if (catalog.isEmpty())
    {
        ScreenCatalogEntry fallback;
        fallback.id = QStringLiteral("screen-0");
        fallback.index = 0;
        fallback.primary = true;
        fallback.rect = QRect(0, 0, 1920, 1080);
        catalog.append(fallback);
    }

    return catalog;
}
}

void WebRtcCli::initializeScreenSelection()
{
    const QVector<ScreenCatalogEntry> catalog = buildScreenCatalogSnapshot();
    const ScreenCatalogEntry *selected = nullptr;
    for (const ScreenCatalogEntry &entry : catalog)
    {
        if (entry.index == m_screenIndex)
        {
            selected = &entry;
            break;
        }
    }
    if (!selected && !catalog.isEmpty())
        selected = &catalog.first();
    if (!selected)
        return;

    m_screenIndex = selected->index;
    m_currentDesktopSourceIndex = selected->index;
    m_currentDesktopSourceId = selected->sourceId;
    m_currentDesktopSourceHasId = selected->hasSourceId;
    m_screenId = selected->id;
    m_currentDesktopSourceRect = selected->rect;
    m_screen_width = qMax(1, selected->rect.width());
    m_screen_height = qMax(1, selected->rect.height());
    calculateEncodeResolution(m_requestedEncodeWidth, m_requestedEncodeHeight);
    LOG_INFO("Initial desktop capture screen selected: id={}, index={}, sourceId={}, hasSourceId={}, size={}x{}",
             m_screenId,
             m_screenIndex,
             m_currentDesktopSourceId,
             m_currentDesktopSourceHasId,
             m_screen_width,
             m_screen_height);
}

QString WebRtcCli::currentScreenId() const
{
    return m_screenId;
}

int WebRtcCli::screenIndexForId(const QString &screenId) const
{
    if (screenId.isEmpty())
        return -1;
    const QVector<ScreenCatalogEntry> catalog = buildScreenCatalogSnapshot();
    for (const ScreenCatalogEntry &entry : catalog)
    {
        if (entry.id == screenId)
            return entry.index;
    }
    return -1;
}

int WebRtcCli::desktopSourceIndexForScreenIndex(int screenIndex) const
{
    const QVector<ScreenCatalogEntry> catalog = buildScreenCatalogSnapshot();
    for (const ScreenCatalogEntry &entry : catalog)
    {
        if (entry.index == screenIndex)
            return entry.index;
    }
    return screenIndex;
}

bool WebRtcCli::desktopSourceIdForScreenIndex(int screenIndex, intptr_t *sourceId) const
{
    if (!sourceId)
        return false;
    const QVector<ScreenCatalogEntry> catalog = buildScreenCatalogSnapshot();
    for (const ScreenCatalogEntry &entry : catalog)
    {
        if (entry.index == screenIndex && entry.hasSourceId)
        {
            *sourceId = entry.sourceId;
            return true;
        }
    }
    return false;
}

QRect WebRtcCli::desktopSourceRectForScreenIndex(int screenIndex) const
{
    const QVector<ScreenCatalogEntry> catalog = buildScreenCatalogSnapshot();
    for (const ScreenCatalogEntry &entry : catalog)
    {
        if (entry.index == screenIndex)
            return entry.rect;
    }
    return QRect();
}

QJsonArray WebRtcCli::screenCatalogJson() const
{
    QJsonArray screensJson;
    const QVector<ScreenCatalogEntry> catalog = buildScreenCatalogSnapshot();
    for (const ScreenCatalogEntry &entry : catalog)
    {
        QJsonObject screenJson;
        screenJson.insert(Constant::KEY_SCREEN_ID, entry.id);
        screenJson.insert(Constant::KEY_SCREEN_INDEX, entry.index);
        screenJson.insert(Constant::KEY_SCREEN_PRIMARY, entry.primary);
        screenJson.insert(Constant::KEY_WIDTH, qMax(1, entry.rect.width()));
        screenJson.insert(Constant::KEY_HEIGHT, qMax(1, entry.rect.height()));
        if (!entry.name.isEmpty())
            screenJson.insert(QStringLiteral("name"), entry.name);
        screensJson.append(screenJson);

        LOG_DEBUG("Screen catalog candidate: id={}, index={}, primary={}, hasSourceId={}, sourceId={}, name={}, rect={}x{}+{}+{}",
                  entry.id,
                  entry.index,
                  entry.primary,
                  entry.hasSourceId,
                  entry.sourceId,
                  entry.name,
                  entry.rect.width(),
                  entry.rect.height(),
                  entry.rect.x(),
                  entry.rect.y());
    }

    return screensJson;
}


void WebRtcCli::handleStreamConfig(const QJsonObject &object)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        m_pendingStreamConfig.insert(it.key(), it.value());

    if (m_pendingStreamConfig.isEmpty())
        return;

    if (!m_streamConfigApplyTimer)
    {
        applyPendingStreamConfig();
        return;
    }

    m_streamConfigApplyTimer->start(kStreamConfigApplyDelayMs);
    LOG_DEBUG("Queued stream config update for coalesced apply in {} ms", kStreamConfigApplyDelayMs);
}


void WebRtcCli::applyPendingStreamConfig()
{
    if (m_pendingStreamConfig.isEmpty())
        return;

    const QJsonObject object = m_pendingStreamConfig;
    m_pendingStreamConfig = QJsonObject();
    applyStreamConfig(object);
}


void WebRtcCli::applyStreamConfig(const QJsonObject &object)
{
    bool streamChanged = false;
    bool shouldNotify = false;

    if (object.contains(Constant::KEY_NETWORK_PATH))
    {
        const QString requestedNetworkPath = JsonUtil::getString(object, Constant::KEY_NETWORK_PATH, m_networkPath)
                                                 .trimmed()
                                                 .toLower();
        const bool validNetworkPath = requestedNetworkPath == QStringLiteral("auto") ||
                                      requestedNetworkPath == QStringLiteral("direct") ||
                                      requestedNetworkPath == QStringLiteral("turn_udp") ||
                                      requestedNetworkPath == QStringLiteral("turn_tcp");
        const QString normalizedNetworkPath = validNetworkPath ? requestedNetworkPath : QStringLiteral("auto");
        if (normalizedNetworkPath != m_networkPath)
        {
            m_networkPath = normalizedNetworkPath;
            shouldNotify = true;
            LOG_INFO("Stream network path directive updated to {}", m_networkPath);
        }
    }

    if (object.contains(Constant::KEY_MEDIA_TOPOLOGY))
    {
        const QString requestedMediaTopology = JsonUtil::getString(object, Constant::KEY_MEDIA_TOPOLOGY, m_mediaTopology)
                                                   .trimmed()
                                                   .toLower();
        const QString normalizedMediaTopology = requestedMediaTopology == QStringLiteral("sfu")
                                                    ? QStringLiteral("sfu")
                                                    : QStringLiteral("p2p");
        if (normalizedMediaTopology != m_mediaTopology)
        {
            m_mediaTopology = normalizedMediaTopology;
            shouldNotify = true;
            LOG_INFO("Stream media topology directive updated to {}", m_mediaTopology);
        }
    }

    if (object.contains(Constant::KEY_QUALITY_PROFILE))
    {
        const QString requestedQuality = JsonUtil::getString(object,
                                                            Constant::KEY_QUALITY_PROFILE,
                                                            m_autoQualityProfile ? QStringLiteral("auto") : m_qualityProfile)
                                             .trimmed()
                                             .toLower();
        QString normalizedQuality = QStringLiteral("lan_hd");
        const bool autoQuality = requestedQuality.isEmpty() || requestedQuality == QStringLiteral("auto");
        if (autoQuality)
            normalizedQuality = QStringLiteral("weak_clear");
        else if (requestedQuality == QStringLiteral("balanced"))
            normalizedQuality = QStringLiteral("balanced");
        else if (requestedQuality == QStringLiteral("weak") ||
                 requestedQuality == QStringLiteral("weak_clear") ||
                 requestedQuality == QStringLiteral("lowbandwidth") ||
                 requestedQuality == QStringLiteral("clear"))
            normalizedQuality = QStringLiteral("weak_clear");
        if (autoQuality != m_autoQualityProfile || normalizedQuality != m_qualityProfile)
        {
            m_autoQualityProfile = autoQuality;
            m_qualityProfile = normalizedQuality;
            m_qualityPoorScore = 0;
            m_qualityGoodScore = 0;
            m_lastQualityProfileSwitchMs = 0;
            if (m_videoTrack)
                m_videoTrack->setDesktopQualityProfile(m_qualityProfile.toStdString());
            shouldNotify = true;
            LOG_INFO("Video quality profile changed to {}, autoQuality={}", m_qualityProfile, m_autoQualityProfile);
        }
    }

    if (object.contains(Constant::KEY_WIDTH) && object.contains(Constant::KEY_HEIGHT))
    {
        const int requestedWidth = JsonUtil::getInt(object, Constant::KEY_WIDTH, 0);
        const int requestedHeight = JsonUtil::getInt(object, Constant::KEY_HEIGHT, 0);
        if (requestedWidth != m_baseRequestedEncodeWidth || requestedHeight != m_baseRequestedEncodeHeight)
        {
            applyRequestedResolution(requestedWidth, requestedHeight);
            streamChanged = true;
            shouldNotify = true;
        }
    }

    if (object.contains(Constant::KEY_FPS))
    {
        const int requestedFps = qBound(kMinAdaptiveFps, JsonUtil::getInt(object, Constant::KEY_FPS, m_fps), 120);
        if (requestedFps != m_fps)
        {
            m_fps = requestedFps;
            calculateEncodeResolution(m_requestedEncodeWidth, m_requestedEncodeHeight);
            applyEffectiveVideoFpsIfNeeded("stream-config-fps");
            streamChanged = true;
            shouldNotify = true;
            LOG_INFO("Video fps constraint changed to requested={}, effective={}, pipelineLimit={}",
                     m_fps, effectiveCaptureFps(), currentPipelineFpsLimit());
        }
    }

    if (streamChanged)
        LOG_INFO("Stream constraints changed; desktop source updated to visible={}x{}, fps={}",
                 m_visible_width,
                 m_visible_height,
                 effectiveCaptureFps());

    if (shouldNotify || streamChanged)
        notifyCurrentStreamConfig();
}


void WebRtcCli::handleSwitchScreen(const QJsonObject &object)
{
    const QString requestedScreenId = JsonUtil::getString(object, Constant::KEY_SCREEN_ID);
    if (requestedScreenId.isEmpty())
    {
        LOG_WARN("Switch screen ignored: screenId is required");
        return;
    }

    selectScreenById(requestedScreenId, true);
}


void WebRtcCli::applyRequestedResolution(int requestedWidth, int requestedHeight)
{
    if (requestedWidth < 0 || requestedHeight < 0)
    {
        m_requestedEncodeWidth = -1;
        m_requestedEncodeHeight = -1;
        m_baseRequestedEncodeWidth = requestedWidth;
        m_baseRequestedEncodeHeight = requestedHeight;
        calculateEncodeResolution(-1, -1);
        LOG_INFO("Transfer resolution changed to original visible resolution: {}x{} (coded {}x{})",
                 m_visible_width, m_visible_height, m_coded_width, m_coded_height);
    }
    else
    {
        m_requestedEncodeWidth = requestedWidth;
        m_requestedEncodeHeight = requestedHeight;
        m_baseRequestedEncodeWidth = requestedWidth;
        m_baseRequestedEncodeHeight = requestedHeight;
        calculateEncodeResolution(m_requestedEncodeWidth, m_requestedEncodeHeight);
        LOG_INFO("Transfer resolution requested by control side: {}x{}, visible {}x{}, coded {}x{}",
                 requestedWidth, requestedHeight, m_visible_width, m_visible_height, m_coded_width, m_coded_height);
    }

    if (m_subscribed)
    {
        if (m_videoTrack)
            m_videoTrack->setDesktopTargetResolution(m_visible_width, m_visible_height, effectiveCaptureFps());
        LOG_INFO("Resolution constraint changed; desktop source output constraint updated immediately");
    }
}


void WebRtcCli::selectScreenById(const QString &screenId, bool updateTrack)
{
    const int requestedIndex = screenIndexForId(screenId);
    if (requestedIndex < 0)
    {
        LOG_WARN("Switch screen ignored: unknown screenId={}", screenId);
        notifyCurrentStreamConfig();
        return;
    }
    const QString resolvedId = screenId;
    const int newScreenIndex = screenIndexForId(resolvedId);
    const int newDesktopSourceIndex = desktopSourceIndexForScreenIndex(newScreenIndex);
    intptr_t newDesktopSourceId = 0;
    const bool hasDesktopSourceId = desktopSourceIdForScreenIndex(newScreenIndex, &newDesktopSourceId);
    const QRect desktopSourceRect = desktopSourceRectForScreenIndex(newScreenIndex);
    if (!desktopSourceRect.isValid())
    {
        LOG_WARN("Switch screen ignored: no valid screen rect for screenId={}, index={}", screenId, newScreenIndex);
        notifyCurrentStreamConfig();
        return;
    }
    const QSize screenSize = desktopSourceRect.size();
    const int oldScreenIndex = m_screenIndex;
    const QString oldScreenId = currentScreenId();
    const bool sameSelection = resolvedId == oldScreenId && newScreenIndex == oldScreenIndex;
    const bool sourceShapeChanged = newDesktopSourceIndex != m_currentDesktopSourceIndex ||
                                    desktopSourceRect != m_currentDesktopSourceRect;
    if (sameSelection && !sourceShapeChanged)
    {
        LOG_DEBUG("Switch screen ignored: screen already selected: id={}, index={}", resolvedId, newScreenIndex);
        notifyCurrentStreamConfig();
        return;
    }

    bool switched = true;
    if (updateTrack && m_videoTrack)
    {
        switched = hasDesktopSourceId
                       ? m_videoTrack->switchDesktopSourceId(newDesktopSourceId)
                       : m_videoTrack->switchDesktopSource(newDesktopSourceIndex);
    }
    if (!switched)
    {
        LOG_WARN("Switch screen failed: oldId={}, oldIndex={}, requestedId={}, requestedIndex={}, desktopSourceIndex={}, hasDesktopSourceId={}, desktopSourceId={}",
                 oldScreenId,
                 oldScreenIndex,
                 screenId,
                 newScreenIndex,
                 newDesktopSourceIndex,
                 hasDesktopSourceId,
                 newDesktopSourceId);
        notifyCurrentStreamConfig();
        return;
    }

    m_screenIndex = newScreenIndex;
    m_currentDesktopSourceIndex = newDesktopSourceIndex;
    m_currentDesktopSourceId = newDesktopSourceId;
    m_currentDesktopSourceHasId = hasDesktopSourceId;
    m_screenId = resolvedId;
    m_currentDesktopSourceRect = desktopSourceRect;
    m_screen_width = screenSize.width();
    m_screen_height = screenSize.height();
    calculateEncodeResolution(m_requestedEncodeWidth, m_requestedEncodeHeight);
    if (updateTrack && m_videoTrack)
    {
        m_videoTrack->setDesktopTargetResolution(m_visible_width, m_visible_height, effectiveCaptureFps());
        m_videoTrack->requestKeyFrame();
    }
    notifyCurrentStreamConfig();

    LOG_INFO("Selected screen from {} ({}) to {} ({}) with desktopSourceIndex={}, hasDesktopSourceId={}, desktopSourceId={}",
             oldScreenId,
             oldScreenIndex,
             m_screenId,
             m_screenIndex,
             newDesktopSourceIndex,
             hasDesktopSourceId,
             newDesktopSourceId);
}
