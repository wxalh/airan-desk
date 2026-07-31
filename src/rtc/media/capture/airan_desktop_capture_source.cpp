#include "rtc/media/capture/airan_desktop_capture_source.h"

#include "common/logger_manager.h"
#include "util/json/bounded_json_line_reader.h"
#include "rtc/core/native_d3d11_video_frame_buffer.h"
#include "util/config/config_util.h"
#include "util/input/input_util.h"

#include "desktop_capture/desktop_capture_options.h"
#include "desktop_capture/desktop_capture_types.h"
#include "desktop_capture/desktop_frame.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#if defined(WEBRTC_WIN)
#include "desktop_capture/win/screen_capture_utils.h"
#include <windows.h>
#endif

#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>
#include <api/video/video_frame_buffer.h>
#include <libyuv/convert.h>
#include <libyuv/scale.h>
#include <rtc_base/ref_counted_object.h>
#include <rtc_base/time_utils.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>
#if AIRAN_WEBRTC_MILESTONE < 144
#include <absl/types/optional.h>
#endif

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include <d3d11.h>
#endif

namespace rtc
{
namespace
{
#if AIRAN_WEBRTC_MILESTONE >= 144
template <typename T>
using WebRtcOptional = std::optional<T>;

template <typename T>
WebRtcOptional<T> webRtcNullopt()
{
    return std::nullopt;
}

int64_t currentTimeMicros()
{
    return webrtc::TimeMicros();
}
#else
template <typename T>
using WebRtcOptional = absl::optional<T>;

template <typename T>
WebRtcOptional<T> webRtcNullopt()
{
    return absl::nullopt;
}

int64_t currentTimeMicros()
{
    return TimeMicros();
}
#endif

constexpr int kMinimumUsefulSecureDesktopScore = 250;
constexpr int kSecureDesktopMaxFrameWidth = 1920;
constexpr int kSecureDesktopMaxFrameHeight = 1080;
constexpr auto kSecureDesktopSuccessInterval = std::chrono::milliseconds(120);
constexpr auto kSecureDesktopRejectedInterval = std::chrono::milliseconds(250);
constexpr auto kSecureDesktopFailureInterval = std::chrono::milliseconds(250);
constexpr auto kCaptureRecreateRetryInterval = std::chrono::milliseconds(500);

#if defined(WEBRTC_WIN)
bool readSecureDesktopPixelsFromSharedMemory(const QJsonObject &header, QByteArray *pixels, QString *errorMessage)
{
    if (!pixels)
        return false;
    pixels->clear();
    const QString name = header.value(QStringLiteral("sharedMemoryName")).toString();
    const int bytes = header.value(QStringLiteral("sharedMemoryBytes")).toInt(header.value(QStringLiteral("bytes")).toInt());
    if (name.isEmpty() || bytes <= 0)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("AiranDesktopCaptureSource", "invalid shared memory header");
        return false;
    }

    const std::wstring mappingName = name.toStdWString();
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName.c_str());
    if (!mapping)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("AiranDesktopCaptureSource", "OpenFileMapping failed: %1").arg(GetLastError());
        return false;
    }

    const void *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(bytes));
    if (!view)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("AiranDesktopCaptureSource", "MapViewOfFile failed: %1").arg(GetLastError());
        CloseHandle(mapping);
        return false;
    }

    pixels->resize(bytes);
    memcpy(pixels->data(), view, static_cast<size_t>(bytes));
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return true;
}

void sendSecureDesktopSharedMemoryAck(QLocalSocket *socket, const QJsonObject &header)
{
    if (!socket)
        return;
    const QString name = header.value(QStringLiteral("sharedMemoryName")).toString();
    if (name.isEmpty())
        return;
    QJsonObject ack;
    ack.insert(QStringLiteral("type"), QStringLiteral("secureCaptureFrameRead"));
    ack.insert(QStringLiteral("sharedMemoryName"), name);
    socket->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n');
    socket->waitForBytesWritten(500);
}
#endif

int normalizedFps(int fps)
{
    return (std::max)(1, fps);
}

std::chrono::milliseconds frameInterval(int fps)
{
    return std::chrono::milliseconds((std::max)(1, 1000 / normalizedFps(fps)));
}

airan::desktop_capture::DesktopCaptureOptions makeAiranDesktopCaptureOptions()
{
    auto options = airan::desktop_capture::DesktopCaptureOptions::CreateDefault();
    options.set_detect_updated_region(true);
    options.set_prefer_cursor_embedded(true);
#if defined(RTC_ENABLE_WIN_WGC)
    options.set_allow_wgc_screen_capturer(ConfigUtil->enable_wgc_capture);
    options.set_allow_wgc_window_capturer(ConfigUtil->enable_wgc_capture);
    options.set_allow_wgc_capturer_fallback(ConfigUtil->enable_wgc_capture);
    options.set_allow_wgc_zero_hertz(true);
    options.set_wgc_require_border(false);
    options.set_wgc_include_secondary_windows(true);
#endif
#if defined(WEBRTC_WIN) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    options.set_allow_directx_capturer(ConfigUtil->enable_dxgi_capture);
    options.set_allow_dxgi_native_gpu_frames(ConfigUtil->enable_dxgi_native_gpu_capture);
    options.set_allow_cropping_window_capturer(false);
#elif defined(WEBRTC_WIN)
    options.set_allow_directx_capturer(false);
    options.set_allow_dxgi_native_gpu_frames(false);
    options.set_allow_cropping_window_capturer(false);
#endif
#if defined(WEBRTC_USE_PIPEWIRE)
    options.set_allow_pipewire(true);
#endif
#if defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
    options.set_allow_sck_capturer(true);
    options.set_allow_iosurface(true);
#endif
#if defined(RTC_ENABLE_WIN_WGC)
    const bool wgcCompiled = true;
    const bool wgcEnabled = ConfigUtil->enable_wgc_capture;
#else
    const bool wgcCompiled = false;
    const bool wgcEnabled = false;
#endif
#if defined(WEBRTC_WIN) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    const bool dxgiEnabled = ConfigUtil->enable_dxgi_capture;
    const bool dxgiNativeGpuEnabled = ConfigUtil->enable_dxgi_native_gpu_capture;
#else
    const bool dxgiEnabled = false;
    const bool dxgiNativeGpuEnabled = false;
#endif
    LOG_INFO("Airan desktop capture options: wgcCompiled={}, wgcEnabled={}, dxgiEnabled={}, dxgiNativeGpuEnabled={}, detectUpdatedRegion={}, preferCursorEmbedded={}",
             wgcCompiled,
             wgcEnabled,
             dxgiEnabled,
             dxgiNativeGpuEnabled,
             true,
             true);
    return options;
}

bool isGpuFrame(const airan::media::CaptureFrameDescriptor &frame)
{
    return frame.frame_kind == airan::media::FrameKind::NativeGpu &&
           frame.native_handle.handle && frame.visible_rect.width > 0 &&
           frame.visible_rect.height > 0;
}

bool isD3D11GpuFrame(const airan::media::CaptureFrameDescriptor &frame)
{
    return isGpuFrame(frame) &&
           frame.native_handle.type == airan::media::NativeHandleType::D3D11Texture2D;
}

std::string captureBackendFromCapturerId(uint32_t capturerId)
{
    using airan::desktop_capture::DesktopCapturerId::kScreenCapturerWinDirectx;
    using airan::desktop_capture::DesktopCapturerId::kScreenCapturerWinGdi;
    using airan::desktop_capture::DesktopCapturerId::kScreenCapturerWinMagnifier;
    using airan::desktop_capture::DesktopCapturerId::kSecureDesktopCapturerWin;
    using airan::desktop_capture::DesktopCapturerId::kWaylandCapturerLinux;
    using airan::desktop_capture::DesktopCapturerId::kWgcCapturerWin;
    using airan::desktop_capture::DesktopCapturerId::kWindowCapturerWinGdi;
    using airan::desktop_capture::DesktopCapturerId::kX11CapturerLinux;

    switch (capturerId)
    {
    case kWgcCapturerWin: return "wgc";
    case kScreenCapturerWinDirectx: return "dxgi";
    case kScreenCapturerWinGdi:
    case kWindowCapturerWinGdi: return "gdi";
    case kScreenCapturerWinMagnifier: return "magnifier";
    case kSecureDesktopCapturerWin: return "secure";
    case kX11CapturerLinux: return "xorg";
    case kWaylandCapturerLinux: return "pipewire";
    default: break;
    }
    return {};
}

} // namespace

bool isAiranDesktopCaptureSourceAvailable()
{
#if defined(WEBRTC_WIN)
    return true;
#elif defined(WEBRTC_USE_PIPEWIRE) || defined(WEBRTC_USE_X11) || (defined(WEBRTC_MAC) && !defined(WEBRTC_IOS))
    return true;
#else
    return false;
#endif
}

scoped_refptr<DesktopVideoSource> createAiranDesktopCaptureSource(const Description::Video &desc)
{
#if !defined(WEBRTC_WIN) && !defined(WEBRTC_USE_PIPEWIRE) && !defined(WEBRTC_USE_X11) && !(defined(WEBRTC_MAC) && !defined(WEBRTC_IOS))
    if (!isAiranDesktopCaptureSourceAvailable())
        return nullptr;
#endif
#if AIRAN_WEBRTC_MILESTONE >= 144
    return webrtc::scoped_refptr<DesktopVideoSource>(
        new webrtc::RefCountedObject<AiranDesktopCaptureSource>(desc));
#else
    return ::rtc::scoped_refptr<DesktopVideoSource>(
        new ::rtc::RefCountedObject<AiranDesktopCaptureSource>(desc));
#endif
}

AiranDesktopCaptureSource::AiranDesktopCaptureSource(const Description::Video &desc)
    : m_sourceIndex(desc.desktopSourceIndex() < 0 ? -1 : desc.desktopSourceIndex()),
      m_sourceId(desc.desktopSourceId()),
      m_hasSourceId(desc.hasDesktopSourceId()),
      m_fps(normalizedFps(desc.desktopFps())),
      m_targetWidth(desc.desktopTargetWidth()),
      m_targetHeight(desc.desktopTargetHeight())
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    configureOutputFormatLocked();
}

AiranDesktopCaptureSource::~AiranDesktopCaptureSource()
{
    stop();
}

bool AiranDesktopCaptureSource::start()
{
    if (m_running.exchange(true))
        return true;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_captureFallback = airan::media::CaptureFallbackStateMachine{};
        m_currentEncodePath = airan::media::EncodePath::GpuZeroCopyEncode;
    }
    SetState(webrtc::MediaSourceInterface::SourceState::kLive);
    m_captureThread = std::thread(&AiranDesktopCaptureSource::captureLoop, this);
    return true;
}

void AiranDesktopCaptureSource::stop()
{
    const bool wasRunning = m_running.exchange(false);
    if (!wasRunning && !m_captureThread.joinable())
        return;
    if (m_captureThread.joinable())
        m_captureThread.join();
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_width = 0;
        m_height = 0;
        m_airanDescriptorInCurrentCapture = false;
        m_nativeDescriptorInCurrentCapture = false;
        m_webrtcFrameInCurrentCapture = false;
    }
    SetState(webrtc::MediaSourceInterface::SourceState::kEnded);
}

bool AiranDesktopCaptureSource::isRunning() const
{
    return m_running.load();
}

bool AiranDesktopCaptureSource::switchSource(int sourceIndex)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_sourceIndex = sourceIndex < 0 ? -1 : sourceIndex;
    m_hasSourceId = false;
    if (!m_capturer)
        return true;
    const bool ok = selectSourceLocked();
    const auto transition = m_captureFallback.markSourceSwitch();
    onAiranCaptureTransition(transition);
    return ok;
}

bool AiranDesktopCaptureSource::switchSourceId(intptr_t sourceId)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_sourceId = sourceId;
    m_hasSourceId = true;
    if (!m_capturer)
        return true;
    const bool ok = selectSourceLocked();
    const auto transition = m_captureFallback.markSourceSwitch();
    onAiranCaptureTransition(transition);
    return ok;
}

bool AiranDesktopCaptureSource::reconfigureCaptureOptions()
{
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_lastSecureDesktopFrameHash.clear();
    }
    if (!m_running.load())
    {
        LOG_INFO("Airan desktop capture options will be applied on next start");
        return true;
    }

    m_recreateCapturerRequested.store(true);
    LOG_INFO("Airan desktop capture options reconfigure queued");
    return true;
}

bool AiranDesktopCaptureSource::GetStats(Stats *stats)
{
    if (!stats)
        return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_width <= 0 || m_height <= 0)
        return false;
    stats->input_width = m_width;
    stats->input_height = m_height;
    return true;
}

airan::media::CaptureCapabilities AiranDesktopCaptureSource::captureCapabilities() const
{
    airan::media::CaptureCapabilities capabilities;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    capabilities.native_gpu = true;
    capabilities.native_handle_types.push_back(airan::media::NativeHandleType::D3D11Texture2D);
#elif defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
    capabilities.native_gpu = true;
    capabilities.native_handle_types.push_back(airan::media::NativeHandleType::IOSurface);
#elif defined(WEBRTC_USE_PIPEWIRE) || defined(WEBRTC_USE_X11)
    capabilities.native_gpu = false;
#else
    capabilities.native_gpu = false;
#endif
    capabilities.cpu = true;
    capabilities.preferred_capture_path = capabilities.native_gpu
                                               ? airan::media::CapturePath::NativeGpuCapture
                                               : airan::media::CapturePath::WebRtcDerivedCpuCapture;
    return capabilities;
}

void AiranDesktopCaptureSource::setAiranCaptureCallback(airan::media::AiranCaptureCallback *callback)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_externalCallback = callback;
}

void AiranDesktopCaptureSource::setFrameSizeCallback(FrameSizeCallback callback)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_frameSizeCallback = std::move(callback);
}

void AiranDesktopCaptureSource::setTargetResolution(int width, int height, int fps)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_targetWidth = width > 0 ? width : 0;
    m_targetHeight = height > 0 ? height : 0;
    m_fps = normalizedFps(fps);
    configureOutputFormatLocked();
    if (m_capturer)
        m_capturer->SetMaxFrameRate(static_cast<uint32_t>(m_fps));
    LOG_DEBUG("Airan desktop capture output constraint updated: target={}x{}, fps={}",
              m_targetWidth,
              m_targetHeight,
              m_fps);
}

void AiranDesktopCaptureSource::captureLoop()
{
    bool capturerUnavailableLogged = false;
    while (m_running.load())
    {
        bool shouldTrySecureCapture = false;
        bool capturedSecureFrame = false;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (!m_capturer)
            {
                if (!createCapturerOnCaptureThread())
                {
                    if (!capturerUnavailableLogged)
                    {
                        LOG_WARN("Airan desktop capture fork source is not available yet; waiting for display/session recovery");
                        capturerUnavailableLogged = true;
                    }
                    shouldTrySecureCapture = shouldTrySecureDesktopFrame();
                }
                else
                {
                    LOG_INFO("Airan desktop capture fork source initialized");
                    capturerUnavailableLogged = false;
                    const auto transition = m_captureFallback.markSourceSwitch();
                    onAiranCaptureTransition(transition);
                    shouldTrySecureCapture = shouldTrySecureDesktopFrame();
                }
            }
            if (m_recreateCapturerRequested.exchange(false))
            {
                LOG_INFO("Recreating Airan desktop capturer with updated options");
                destroyCapturerOnCaptureThread();
                if (!createCapturerOnCaptureThread())
                {
                    LOG_WARN("Airan desktop capture reconfigure failed; capture will retry while the display/session recovers");
                    shouldTrySecureCapture = shouldTrySecureDesktopFrame();
                }
                else
                {
                    capturerUnavailableLogged = false;
                    const auto transition = m_captureFallback.markSourceSwitch();
                    onAiranCaptureTransition(transition);
                    shouldTrySecureCapture = shouldTrySecureDesktopFrame();
                }
            }
            else if (m_capturer)
            {
                shouldTrySecureCapture = shouldTrySecureDesktopFrame();
            }
        }
        if (shouldTrySecureCapture)
        {
            capturedSecureFrame = tryCaptureSecureDesktopFrame();
            shouldTrySecureCapture = false;
        }

        if (!capturedSecureFrame)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (m_capturer)
            {
                m_airanDescriptorInCurrentCapture = false;
                m_nativeDescriptorInCurrentCapture = false;
                m_webrtcFrameInCurrentCapture = false;
                m_capturer->CaptureFrame();
                shouldTrySecureCapture = shouldTrySecureDesktopFrame() &&
                                         !m_airanDescriptorInCurrentCapture &&
                                         !m_nativeDescriptorInCurrentCapture &&
                                         !m_webrtcFrameInCurrentCapture;
            }
        }
        if (shouldTrySecureCapture)
            tryCaptureSecureDesktopFrame();

        int fps = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            fps = m_fps;
        }
        const auto interval = m_capturer ? frameInterval(fps) : kCaptureRecreateRetryInterval;
        std::this_thread::sleep_for(interval);
    }

    destroyCapturerOnCaptureThread();
}

bool AiranDesktopCaptureSource::createCapturerOnCaptureThread()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_captureFallback = airan::media::CaptureFallbackStateMachine{};
    m_currentEncodePath = airan::media::EncodePath::GpuZeroCopyEncode;
    auto options = makeAiranDesktopCaptureOptions();
    m_capturer = airan::desktop_capture::DesktopCapturer::CreateScreenCapturer(options);
    if (!m_capturer)
        return false;
    m_capturer->SetMaxFrameRate(static_cast<uint32_t>(m_fps));
    m_capturer->SetAiranCaptureCallback(this);
    if (!selectSourceLocked())
    {
        m_capturer->SetAiranCaptureCallback(nullptr);
        m_capturer.reset();
        return false;
    }
    m_capturer->Start(this);
    return true;
}

void AiranDesktopCaptureSource::destroyCapturerOnCaptureThread()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_capturer)
        return;
    m_capturer->SetAiranCaptureCallback(nullptr);
    m_capturer.reset();
}

bool AiranDesktopCaptureSource::selectSourceLocked()
{
    if (!m_capturer)
        return false;

    airan::desktop_capture::DesktopCapturer::SourceList sources;
    if (!m_capturer->GetSourceList(&sources))
        return false;

    if (m_hasSourceId)
    {
        int matchedIndex = -1;
        std::string matchedTitle;
        for (int i = 0; i < static_cast<int>(sources.size()); ++i)
        {
            if (sources[static_cast<size_t>(i)].id == static_cast<airan::desktop_capture::DesktopCapturer::SourceId>(m_sourceId))
            {
                matchedIndex = i;
                matchedTitle = sources[static_cast<size_t>(i)].title;
                break;
            }
        }

        const auto selected = static_cast<airan::desktop_capture::DesktopCapturer::SourceId>(m_sourceId);
        const bool ok = m_capturer->SelectSource(selected);
        LOG_INFO("Airan desktop capture fork selected screen by source id: requestedSourceId={}, matchedIndex={}, sourceCount={}, title={}, result={}",
                 m_sourceId,
                 matchedIndex,
                 sources.size(),
                 matchedTitle,
                 ok ? "ok" : "failed");
        return ok;
    }

    if (sources.empty())
        return false;

    int selectedIndex = 0;
    if (m_sourceIndex >= 0)
    {
        const int index = m_sourceIndex;
        if (index < static_cast<int>(sources.size()))
            selectedIndex = index;
        else
        {
            LOG_WARN("Airan desktop capture source index is out of range: requestedIndex={}, sourceCount={}",
                     m_sourceIndex,
                     sources.size());
            return false;
        }
    }
    else
    {
#if defined(WEBRTC_WIN)
        for (int i = 0; i < static_cast<int>(sources.size()); ++i)
        {
            const auto rect = airan::desktop_capture::GetScreenRect(sources[static_cast<size_t>(i)].id, std::nullopt);
            if (!rect.is_empty() && rect.left() == 0 && rect.top() == 0)
            {
                selectedIndex = i;
                break;
            }
        }
#endif
    }

    const auto &sourceInfo = sources[static_cast<size_t>(selectedIndex)];
    const auto selected = sourceInfo.id;
    const bool ok = m_capturer->SelectSource(selected);
    LOG_INFO("Airan desktop capture fork selected screen: requestedIndex={}, actualIndex={}, sourceCount={}, sourceId={}, title={}, result={}",
             m_sourceIndex,
             selectedIndex,
             sources.size(),
             static_cast<intptr_t>(selected),
             sourceInfo.title,
             ok ? "ok" : "failed");
    return ok;
}

#if defined(WEBRTC_WIN)
std::optional<airan::desktop_capture::DesktopRect> AiranDesktopCaptureSource::selectedSourceRectLocked() const
{
    auto rectForSource = [](airan::desktop_capture::DesktopCapturer::SourceId sourceId)
        -> std::optional<airan::desktop_capture::DesktopRect> {
        const auto rect = airan::desktop_capture::GetScreenRect(sourceId, std::nullopt);
        if (rect.is_empty())
            return std::nullopt;
        return rect;
    };

    if (m_hasSourceId)
        return rectForSource(static_cast<airan::desktop_capture::DesktopCapturer::SourceId>(m_sourceId));

    airan::desktop_capture::DesktopCapturer::SourceList sources;
    if (airan::desktop_capture::GetScreenList(&sources) && !sources.empty())
    {
        int selectedIndex = m_sourceIndex >= 0 ? m_sourceIndex : 0;
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(sources.size()))
        {
            if (auto rect = rectForSource(sources[static_cast<size_t>(selectedIndex)].id))
                return rect;
        }
    }

    airan::desktop_capture::DesktopCapturer::SourceList monitorSources;
    if (airan::desktop_capture::GetAiranMonitorList(&monitorSources) && !monitorSources.empty())
    {
        int selectedIndex = m_sourceIndex >= 0 ? m_sourceIndex : 0;
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(monitorSources.size()))
        {
            if (auto rect = rectForSource(monitorSources[static_cast<size_t>(selectedIndex)].id))
                return rect;
        }
    }

    return rectForSource(airan::desktop_capture::GetPrimaryScreenId());
}
#endif

void AiranDesktopCaptureSource::configureOutputFormatLocked()
{
    const WebRtcOptional<int> maxFps = m_fps > 0 ? WebRtcOptional<int>(m_fps) : webRtcNullopt<int>();
    if (m_targetWidth > 0 && m_targetHeight > 0)
    {
        video_adapter()->OnOutputFormatRequest(WebRtcOptional<std::pair<int, int>>(std::pair<int, int>(m_targetWidth, m_targetHeight)),
                                               WebRtcOptional<int>(m_targetWidth * m_targetHeight),
                                               maxFps);
    }
    else
    {
        video_adapter()->OnOutputFormatRequest(webRtcNullopt<std::pair<int, int>>(), webRtcNullopt<int>(), maxFps);
    }
}

bool AiranDesktopCaptureSource::adaptFrame(int width,
                                           int height,
                                           int64_t timestampUs,
                                           FrameAdaptation *adaptation)
{
    if (!adaptation || width <= 0 || height <= 0)
        return false;
    if (!AdaptFrame(width,
                    height,
                    timestampUs,
                    &adaptation->outputWidth,
                    &adaptation->outputHeight,
                    &adaptation->cropWidth,
                    &adaptation->cropHeight,
                    &adaptation->cropX,
                    &adaptation->cropY))
    {
        return false;
    }
    return adaptation->outputWidth > 0 &&
           adaptation->outputHeight > 0 &&
           adaptation->cropWidth > 0 &&
           adaptation->cropHeight > 0;
}

void AiranDesktopCaptureSource::emitCpuFrameDescriptor(const airan::desktop_capture::DesktopFrame &frame,
                                                       int64_t timestampUs,
                                                       const airan::media::PathTransition &transition)
{
    airan::media::AiranCaptureCallback *callback = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        callback = m_externalCallback;
    }
    if (!callback)
        return;

    const int width = frame.size().width();
    const int height = frame.size().height();
    const int stride = frame.stride();
    if (width <= 0 || height <= 0 || stride <= 0 || !frame.data())
        return;

    auto ownedPixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(stride) *
                                                              static_cast<size_t>(height));
    std::memcpy(ownedPixels->data(), frame.data(), ownedPixels->size());

    airan::media::CaptureFrameDescriptor descriptor;
    descriptor.frame_kind = airan::media::FrameKind::CpuBgra;
    descriptor.capture_backend = captureBackendFromCapturerId(frame.capturer_id());
    descriptor.native_handle.type = airan::media::NativeHandleType::kNone;
    descriptor.visible_rect = {0, 0, width, height};
    descriptor.coded_size = {width, height};
    descriptor.planes.push_back({ownedPixels->data(), stride, 0, width, height, 0});
    descriptor.color_space.primaries = airan::media::ColorValue::Bt709;
    descriptor.color_space.transfer = airan::media::ColorValue::Bt709;
    descriptor.color_space.matrix = airan::media::ColorValue::Bt709;
    descriptor.color_space.range = airan::media::ColorRange::Limited;
    descriptor.timestamp = std::chrono::microseconds(timestampUs);
    descriptor.current_capture_path = transition.current_capture_path;
    descriptor.current_encode_path = transition.current_encode_path;
    descriptor.fallback_reason = transition.fallback_reason;
    descriptor.release = airan::media::makeFrameRelease([ownedPixels]() {});
    callback->onAiranCaptureFrame(std::move(descriptor));
}

void AiranDesktopCaptureSource::broadcastCpuFrame(const airan::desktop_capture::DesktopFrame &frame,
                                                  int64_t timestampUs)
{
    const int width = frame.size().width();
    const int height = frame.size().height();
    FrameAdaptation adaptation;
    if (!adaptFrame(width, height, timestampUs, &adaptation))
    {
        OnFrameDropped();
        return;
    }

    auto buffer = webrtc::I420Buffer::Create(adaptation.outputWidth, adaptation.outputHeight);
    if (!buffer)
        return;

    const uint8_t *src = frame.data() +
                         static_cast<size_t>(adaptation.cropY) * static_cast<size_t>(frame.stride()) +
                         static_cast<size_t>(adaptation.cropX) * airan::desktop_capture::DesktopFrame::kBytesPerPixel;
    auto cropped = webrtc::I420Buffer::Create(adaptation.cropWidth, adaptation.cropHeight);
    if (!cropped)
        return;

    const int result = libyuv::ARGBToI420(src,
                                          frame.stride(),
                                          cropped->MutableDataY(),
                                          cropped->StrideY(),
                                          cropped->MutableDataU(),
                                          cropped->StrideU(),
                                          cropped->MutableDataV(),
                                          cropped->StrideV(),
                                          adaptation.cropWidth,
                                          adaptation.cropHeight);
    if (result != 0)
    {
        LOG_WARN("Airan desktop capture CPU fallback conversion failed: size={}x{}, crop={}x{}",
                 width,
                 height,
                 adaptation.cropWidth,
                 adaptation.cropHeight);
        return;
    }

    buffer->CropAndScaleFrom(*cropped, 0, 0, adaptation.cropWidth, adaptation.cropHeight);
    auto videoFrame = webrtc::VideoFrame::Builder()
                          .set_video_frame_buffer(buffer)
                          .set_timestamp_us(timestampUs)
                          .set_rotation(webrtc::kVideoRotation_0)
                          .build();
    OnFrame(videoFrame);
}

bool AiranDesktopCaptureSource::tryCaptureSecureDesktopFrame()
{
#if defined(WEBRTC_WIN)
    if (!shouldTrySecureDesktopFrame())
        return false;
    if (!m_secureDesktopAttemptLogged)
    {
        LOG_INFO("Trying secure desktop capture fallback while Windows session is locked");
        m_secureDesktopAttemptLogged = true;
    }

    QLocalSocket socket;
    socket.connectToServer(InputUtil::windowsInputBrokerServerName(), QIODevice::ReadWrite);
    if (!socket.waitForConnected(1000))
    {
        markSecureDesktopCaptureFailed("connect");
        return false;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("secureCaptureFrame"));
    request.insert(QStringLiteral("sharedMemory"), true);
    request.insert(QStringLiteral("maxWidth"), kSecureDesktopMaxFrameWidth);
    request.insert(QStringLiteral("maxHeight"), kSecureDesktopMaxFrameHeight);
    if (!InputUtil::authenticateWindowsInputBrokerRequest(&request))
    {
        markSecureDesktopCaptureFailed("broker-authentication-unavailable");
        return false;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_lastSecureDesktopFrameHash.isEmpty())
            request.insert(QStringLiteral("lastHash"), m_lastSecureDesktopFrameHash);
        const auto selectedRect = selectedSourceRectLocked();
        if (selectedRect.has_value())
        {
            request.insert(QStringLiteral("x"), selectedRect->left());
            request.insert(QStringLiteral("y"), selectedRect->top());
            request.insert(QStringLiteral("width"), selectedRect->width());
            request.insert(QStringLiteral("height"), selectedRect->height());
        }
    }
    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(1000))
    {
        markSecureDesktopCaptureFailed("write");
        return false;
    }
    if (!socket.waitForReadyRead(5000))
    {
        markSecureDesktopCaptureFailed("header-timeout");
        return false;
    }

    QByteArray headerLine;
    if (!readBoundedJsonLine(&socket, &headerLine, 4096, 5000))
    {
        markSecureDesktopCaptureFailed("header-line-timeout-or-too-large");
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument headerDocument = QJsonDocument::fromJson(headerLine.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !headerDocument.isObject())
    {
        markSecureDesktopCaptureFailed("invalid-header");
        return false;
    }
    const QJsonObject header = headerDocument.object();
    if (!InputUtil::isWindowsSessionLocked())
    {
        m_nextSecureDesktopAttempt = {};
        return false;
    }
    if (!header.value(QStringLiteral("ok")).toBool(false))
    {
        const QString error = header.value(QStringLiteral("error")).toString();
        if (error == QStringLiteral("duplicate-frame"))
        {
            m_nextSecureDesktopAttempt = std::chrono::steady_clock::now() + kSecureDesktopSuccessInterval;
            m_secureDesktopCaptureAvailable = true;
            m_secureDesktopFailureLogged = false;
            m_secureDesktopAttemptLogged = false;
            return true;
        }
        if (error == QStringLiteral("low-content-frame") ||
            error == QStringLiteral("blue-placeholder-frame"))
        {
            m_nextSecureDesktopAttempt = std::chrono::steady_clock::now() + kSecureDesktopRejectedInterval;
            if (!m_secureDesktopLowScoreLogged)
            {
                LOG_WARN("Secure desktop capture rejected frame: reason={}, desktop={}, score={}, colors={}, lumaStdDev={}, edgeAvg={}, centralEdge={}, blueRatio={}%, size={}x{}+{}+{}",
                         error,
                         header.value(QStringLiteral("desktop")).toString(),
                         header.value(QStringLiteral("score")).toInt(-1),
                         header.value(QStringLiteral("colors")).toInt(),
                         header.value(QStringLiteral("lumaStdDev")).toInt(),
                         header.value(QStringLiteral("edgeAvg")).toInt(),
                         header.value(QStringLiteral("centralEdge")).toInt(),
                         header.value(QStringLiteral("blueRatio")).toInt(),
                         header.value(QStringLiteral("width")).toInt(),
                         header.value(QStringLiteral("height")).toInt(),
                         header.value(QStringLiteral("x")).toInt(),
                         header.value(QStringLiteral("y")).toInt());
                m_secureDesktopLowScoreLogged = true;
            }
            return false;
        }
        markSecureDesktopCaptureFailed("capture-failed");
        return false;
    }

    const int width = header.value(QStringLiteral("width")).toInt();
    const int height = header.value(QStringLiteral("height")).toInt();
    const int stride = header.value(QStringLiteral("stride")).toInt();
    const int bytes = header.value(QStringLiteral("bytes")).toInt();
    const int transportBytes = header.value(QStringLiteral("sharedMemoryBytes")).toInt(bytes);
    const int virtualX = header.value(QStringLiteral("x")).toInt(0);
    const int virtualY = header.value(QStringLiteral("y")).toInt(0);
    const int score = header.value(QStringLiteral("score")).toInt(-1);
    const QString desktop = header.value(QStringLiteral("desktop")).toString();
    const QString frameHash = header.value(QStringLiteral("hash")).toString();
    static qint64 lastSecureFrameLogMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - lastSecureFrameLogMs > 10000)
    {
        LOG_DEBUG("Secure desktop capture frame header: desktop={}, rect={}, transport={}, score={}, windows={}, painted={}, colors={}, lumaStdDev={}, edgeAvg={}, centralEdge={}, blueRatio={}%, bluePlaceholder={}, scaled={}, original={}x{}, size={}x{}+{}+{}, bytes={}",
                  desktop,
                  header.value(QStringLiteral("rectKind")).toString(),
                  header.value(QStringLiteral("transport")).toString(QStringLiteral("socket")),
                  score,
                  header.value(QStringLiteral("windows")).toInt(),
                  header.value(QStringLiteral("paintedWindows")).toInt(),
                  header.value(QStringLiteral("colors")).toInt(),
                  header.value(QStringLiteral("lumaStdDev")).toInt(),
                  header.value(QStringLiteral("edgeAvg")).toInt(),
                  header.value(QStringLiteral("centralEdge")).toInt(),
                  header.value(QStringLiteral("blueRatio")).toInt(),
                  header.value(QStringLiteral("bluePlaceholder")).toBool(false),
                  header.value(QStringLiteral("scaled")).toBool(false),
                  header.value(QStringLiteral("originalWidth")).toInt(width),
                  header.value(QStringLiteral("originalHeight")).toInt(height),
                  width,
                  height,
                  virtualX,
                  virtualY,
                  bytes);
        lastSecureFrameLogMs = nowMs;
    }
    constexpr int kMaxSecureFrameDimension = 32768;
    constexpr qint64 kMaxSecureFrameBytes = 1024LL * 1024 * 1024;
    const qint64 minimumStride = static_cast<qint64>(width) * 4;
    const qint64 requiredBytes = static_cast<qint64>(stride) * height;
    if (width <= 0 || height <= 0 ||
        width > kMaxSecureFrameDimension || height > kMaxSecureFrameDimension ||
        stride <= 0 || static_cast<qint64>(stride) < minimumStride ||
        requiredBytes <= 0 || requiredBytes > kMaxSecureFrameBytes ||
        bytes < requiredBytes || bytes > kMaxSecureFrameBytes ||
        transportBytes < requiredBytes || transportBytes > kMaxSecureFrameBytes)
    {
        markSecureDesktopCaptureFailed("invalid-frame");
        return false;
    }
    if (score < kMinimumUsefulSecureDesktopScore)
    {
        m_nextSecureDesktopAttempt = std::chrono::steady_clock::now() + kSecureDesktopRejectedInterval;
        if (!m_secureDesktopLowScoreLogged)
        {
            LOG_WARN("Secure desktop capture rejected low-content frame: desktop={}, score={}, colors={}, lumaStdDev={}, edgeAvg={}, centralEdge={}, blueRatio={}%, size={}x{}+{}+{}",
                     desktop,
                     score,
                     header.value(QStringLiteral("colors")).toInt(),
                     header.value(QStringLiteral("lumaStdDev")).toInt(),
                     header.value(QStringLiteral("edgeAvg")).toInt(),
                     header.value(QStringLiteral("centralEdge")).toInt(),
                     header.value(QStringLiteral("blueRatio")).toInt(),
                     width,
                     height,
                     virtualX,
                     virtualY);
            m_secureDesktopLowScoreLogged = true;
        }
        return false;
    }

    QByteArray pixels;
    const QString transport = header.value(QStringLiteral("transport")).toString(QStringLiteral("socket"));
    if (transport == QStringLiteral("sharedMemory"))
    {
        QString sharedMemoryError;
        if (!readSecureDesktopPixelsFromSharedMemory(header, &pixels, &sharedMemoryError))
        {
            LOG_WARN("Secure desktop shared memory frame read failed: {}", sharedMemoryError);
            markSecureDesktopCaptureFailed("shared-memory-read");
            return false;
        }
        sendSecureDesktopSharedMemoryAck(&socket, header);
    }
    else
    {
        pixels.reserve(bytes);
        while (pixels.size() < bytes)
        {
            if (!socket.bytesAvailable() && !socket.waitForReadyRead(5000))
            {
                markSecureDesktopCaptureFailed("pixels-timeout");
                return false;
            }
            pixels += socket.read(bytes - pixels.size());
        }
    }
    if (!InputUtil::isWindowsSessionLocked())
    {
        m_nextSecureDesktopAttempt = {};
        return false;
    }

    int frameX = 0;
    int frameY = 0;
    int frameWidth = width;
    int frameHeight = height;
    const bool brokerCroppedToSelectedSource =
        header.value(QStringLiteral("cropped")).toBool(false) &&
        header.value(QStringLiteral("rectKind")).toString() == QStringLiteral("selected");
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        const auto selectedRect = selectedSourceRectLocked();
        if (brokerCroppedToSelectedSource && selectedRect.has_value())
        {
            const int cropLeft = selectedRect->left() - virtualX;
            const int cropTop = selectedRect->top() - virtualY;
            const int cropRight = cropLeft + selectedRect->width();
            const int cropBottom = cropTop + selectedRect->height();
            const int boundedLeft = (std::max)(0, cropLeft);
            const int boundedTop = (std::max)(0, cropTop);
            const int boundedRight = (std::min)(width, cropRight);
            const int boundedBottom = (std::min)(height, cropBottom);
            if (boundedRight > boundedLeft && boundedBottom > boundedTop)
            {
                frameX = boundedLeft;
                frameY = boundedTop;
                frameWidth = boundedRight - boundedLeft;
                frameHeight = boundedBottom - boundedTop;
                if (frameWidth != width || frameHeight != height)
                {
                    LOG_DEBUG("Secure desktop frame cropped to selected source: sourceRect={}x{}+{}+{}, virtual={}x{}+{}+{}, crop={}x{}+{}+{}",
                              selectedRect->width(),
                              selectedRect->height(),
                              selectedRect->left(),
                              selectedRect->top(),
                              width,
                              height,
                              virtualX,
                              virtualY,
                              frameWidth,
                              frameHeight,
                              frameX,
                              frameY);
                }
            }
            else
            {
                LOG_WARN("Secure desktop selected source rect is outside captured frame: sourceRect={}x{}+{}+{}, virtual={}x{}+{}+{}",
                         selectedRect->width(),
                         selectedRect->height(),
                         selectedRect->left(),
                         selectedRect->top(),
                         width,
                         height,
                         virtualX,
                         virtualY);
            }
        }
    }

    auto frame = std::make_unique<airan::desktop_capture::BasicDesktopFrame>(
        airan::desktop_capture::DesktopSize(frameWidth, frameHeight));
    const auto *sourcePixels = reinterpret_cast<const uint8_t *>(pixels.constData()) +
                               static_cast<size_t>(frameY) * static_cast<size_t>(stride) +
                               static_cast<size_t>(frameX) * airan::desktop_capture::DesktopFrame::kBytesPerPixel;
    frame->CopyPixelsFrom(sourcePixels,
                          stride,
                          airan::desktop_capture::DesktopRect::MakeWH(frameWidth, frameHeight));
    if (!InputUtil::isWindowsSessionLocked())
    {
        m_nextSecureDesktopAttempt = {};
        return false;
    }
    frame->set_capturer_id(airan::desktop_capture::DesktopCapturerId::kSecureDesktopCapturerWin);
    frame->mutable_updated_region()->SetRect(airan::desktop_capture::DesktopRect::MakeWH(frameWidth, frameHeight));
    if (!frameHash.isEmpty())
        m_lastSecureDesktopFrameHash = frameHash;
    markSecureDesktopCaptureSucceeded();
    OnCaptureResult(airan::desktop_capture::DesktopCapturer::Result::SUCCESS, std::move(frame));
    return true;
#else
    return false;
#endif
}

bool AiranDesktopCaptureSource::shouldTrySecureDesktopFrame()
{
    if (!InputUtil::isWindowsSessionLocked())
    {
        m_lastSecureDesktopFrameHash.clear();
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextSecureDesktopAttempt)
    {
        static qint64 lastThrottleLogMs = 0;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - lastThrottleLogMs > 10000)
        {
            LOG_DEBUG("Secure desktop capture fallback throttled while Windows session is locked");
            lastThrottleLogMs = nowMs;
        }
        return false;
    }
    return true;
}

bool AiranDesktopCaptureSource::shouldSuppressLockedPlaceholderFrame(const airan::desktop_capture::DesktopFrame &frame)
{
#if defined(WEBRTC_WIN)
    if (!InputUtil::isWindowsSessionLocked())
    {
        m_lockedPlaceholderSuppressionLogged = false;
        return false;
    }
    if (frame.capturer_id() == airan::desktop_capture::DesktopCapturerId::kSecureDesktopCapturerWin)
        return false;
    if (!m_secureDesktopCaptureAvailable && std::chrono::steady_clock::now() < m_nextSecureDesktopAttempt)
        return false;

    const int width = frame.size().width();
    const int height = frame.size().height();
    const int stride = frame.stride();
    if (width <= 0 || height <= 0 || stride < width * airan::desktop_capture::DesktopFrame::kBytesPerPixel || !frame.data())
        return false;

    constexpr int kGridX = 64;
    constexpr int kGridY = 36;
    std::set<uint32_t> colors;
    std::vector<int> luminance;
    luminance.reserve(kGridX * kGridY);
    int blueCount = 0;

    const auto *data = reinterpret_cast<const uint8_t *>(frame.data());
    for (int gy = 0; gy < kGridY; ++gy)
    {
        const int y = std::clamp((gy * height) / kGridY, 0, height - 1);
        for (int gx = 0; gx < kGridX; ++gx)
        {
            const int x = std::clamp((gx * width) / kGridX, 0, width - 1);
            const uint8_t *pixel = data + static_cast<size_t>(y) * static_cast<size_t>(stride) +
                                   static_cast<size_t>(x) * airan::desktop_capture::DesktopFrame::kBytesPerPixel;
            const int b = pixel[0];
            const int g = pixel[1];
            const int r = pixel[2];
            colors.insert(static_cast<uint32_t>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)));
            luminance.push_back((r * 30 + g * 59 + b * 11) / 100);
            if (b > 120 && b > r + 45 && b > g + 15 && r < 110)
                ++blueCount;
        }
    }

    double mean = 0.0;
    for (int value : luminance)
        mean += value;
    mean /= std::max(1, static_cast<int>(luminance.size()));

    double variance = 0.0;
    for (int value : luminance)
    {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= std::max(1, static_cast<int>(luminance.size()));

    int edgeSum = 0;
    int edgeCount = 0;
    int centralEdgeSum = 0;
    int centralEdgeCount = 0;
    for (int gy = 0; gy < kGridY; ++gy)
    {
        for (int gx = 1; gx < kGridX; ++gx)
        {
            const int index = gy * kGridX + gx;
            const int edge = std::abs(luminance[index] - luminance[index - 1]);
            edgeSum += edge;
            ++edgeCount;
            if (gy >= kGridY / 4 && gy < (kGridY * 3) / 4 &&
                gx >= kGridX / 4 && gx < (kGridX * 3) / 4)
            {
                centralEdgeSum += edge;
                ++centralEdgeCount;
            }
        }
    }

    const int blueRatioPercent = (blueCount * 100) / std::max(1, static_cast<int>(luminance.size()));
    const int lumaStdDev = static_cast<int>(std::sqrt(variance));
    const int averageEdge = edgeCount > 0 ? edgeSum / edgeCount : 0;
    const int centralEdge = centralEdgeCount > 0 ? centralEdgeSum / centralEdgeCount : 0;
    const bool placeholder = blueRatioPercent >= 70 &&
                             colors.size() <= 16 &&
                             lumaStdDev <= 28 &&
                             averageEdge <= 8 &&
                             centralEdge <= 8;
    if (placeholder && !m_lockedPlaceholderSuppressionLogged)
    {
        LOG_WARN("Suppressed locked-session blue placeholder frame: backend={}, blueRatio={}%, colors={}, lumaStdDev={}, edgeAvg={}, centralEdge={}, size={}x{}",
                 captureBackendFromCapturerId(frame.capturer_id()),
                 blueRatioPercent,
                 colors.size(),
                 lumaStdDev,
                 averageEdge,
                 centralEdge,
                 width,
                 height);
        m_lockedPlaceholderSuppressionLogged = true;
    }
    return placeholder;
#else
    Q_UNUSED(frame);
    return false;
#endif
}

void AiranDesktopCaptureSource::markSecureDesktopCaptureFailed(const char *reason)
{
    const bool wasAvailable = m_secureDesktopCaptureAvailable;
    m_secureDesktopCaptureAvailable = false;
    m_nextSecureDesktopAttempt = std::chrono::steady_clock::now() + kSecureDesktopFailureInterval;
    if (wasAvailable || !m_secureDesktopFailureLogged)
    {
        LOG_WARN("Secure desktop capture fallback is not available: {}", reason ? reason : "unknown");
        m_secureDesktopFailureLogged = true;
    }
}

void AiranDesktopCaptureSource::markSecureDesktopCaptureSucceeded()
{
    if (!m_secureDesktopCaptureAvailable)
        LOG_INFO("Secure desktop capture fallback started");
    m_secureDesktopCaptureAvailable = true;
    m_secureDesktopFailureLogged = false;
    m_secureDesktopAttemptLogged = false;
    m_secureDesktopLowScoreLogged = false;
    m_nextSecureDesktopAttempt = std::chrono::steady_clock::now() + kSecureDesktopSuccessInterval;
}

scoped_refptr<webrtc::VideoFrameBuffer> AiranDesktopCaptureSource::adaptVideoBuffer(
    const scoped_refptr<webrtc::VideoFrameBuffer> &source,
    const FrameAdaptation &adaptation)
{
    if (!source)
        return nullptr;
    if (source->width() == adaptation.outputWidth &&
        source->height() == adaptation.outputHeight &&
        adaptation.cropX == 0 &&
        adaptation.cropY == 0 &&
        adaptation.cropWidth == source->width() &&
        adaptation.cropHeight == source->height())
    {
        return source;
    }

    return source->CropAndScale(adaptation.cropX,
                                adaptation.cropY,
                                adaptation.cropWidth,
                                adaptation.cropHeight,
                                adaptation.outputWidth,
                                adaptation.outputHeight);
}

bool AiranDesktopCaptureSource::updateCapturedSizeLocked(int width, int height, const char *label)
{
    if (width <= 0 || height <= 0)
        return false;
    const int oldWidth = m_width;
    const int oldHeight = m_height;
    m_width = width;
    m_height = height;
    if ((oldWidth > 0 || oldHeight > 0) && (oldWidth != m_width || oldHeight != m_height))
    {
        LOG_DEBUG("Airan desktop {} frame size changed: {}x{} -> {}x{}",
                  label ? label : "captured",
                  oldWidth,
                  oldHeight,
                  m_width,
                  m_height);
        return true;
    }
    return false;
}

airan::media::EncodePath AiranDesktopCaptureSource::currentEncodePathLocked() const
{
    return m_currentEncodePath;
}

void AiranDesktopCaptureSource::notifyFrameSizeChanged(int width, int height)
{
    FrameSizeCallback callback;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        callback = m_frameSizeCallback;
    }
    if (callback)
        callback(width, height);
}

void AiranDesktopCaptureSource::OnCaptureResult(airan::desktop_capture::DesktopCapturer::Result result,
                                                std::unique_ptr<airan::desktop_capture::DesktopFrame> frame)
{
    if (result != airan::desktop_capture::DesktopCapturer::Result::SUCCESS || !frame)
    {
        if (tryCaptureSecureDesktopFrame())
            return;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            ++m_consecutiveCaptureFailures;
            if (m_consecutiveCaptureFailures >= 10)
            {
                m_recreateCapturerRequested.store(true);
                m_consecutiveCaptureFailures = 0;
                LOG_WARN("Airan desktop capture has consecutive failures; capturer recreation queued");
            }
        }
        const auto transition = m_captureFallback.markFailure(
            result == airan::desktop_capture::DesktopCapturer::Result::ERROR_PERMANENT
                ? airan::media::FallbackReason::CaptureError
                : airan::media::FallbackReason::CaptureError);
        onAiranCaptureTransition(transition);
        return;
    }

    if (shouldSuppressLockedPlaceholderFrame(*frame))
    {
        if (tryCaptureSecureDesktopFrame())
            return;
    }

    const int64_t timestampUs = currentTimeMicros();
    bool hasAiranDescriptor = false;
    bool hasNativeDescriptor = false;
    bool hasWebRtcFrame = false;
    bool frameSizeChanged = false;
    int frameWidth = 0;
    int frameHeight = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_consecutiveCaptureFailures = 0;
        frameWidth = frame->size().width();
        frameHeight = frame->size().height();
        frameSizeChanged = updateCapturedSizeLocked(frameWidth, frameHeight, "captured");
        hasAiranDescriptor = m_airanDescriptorInCurrentCapture;
        hasNativeDescriptor = m_nativeDescriptorInCurrentCapture;
        hasWebRtcFrame = m_webrtcFrameInCurrentCapture;
    }
    if (frameSizeChanged)
        notifyFrameSizeChanged(frameWidth, frameHeight);
    if (hasAiranDescriptor)
    {
        if (!hasWebRtcFrame)
            broadcastCpuFrame(*frame, timestampUs);
        return;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_captureFallback.setCurrentEncodePath(currentEncodePathLocked());
    }
    const auto transition = m_captureFallback.markCpuFrame(airan::media::FallbackReason::CapabilityMissing);
    onAiranCaptureTransition(transition);
    emitCpuFrameDescriptor(*frame, timestampUs, transition);
    broadcastCpuFrame(*frame, timestampUs);
}

void AiranDesktopCaptureSource::onAiranCaptureFrame(airan::media::CaptureFrameDescriptor frame)
{
    airan::media::AiranCaptureCallback *external = nullptr;
    const bool hasNativeDescriptor = isGpuFrame(frame);
    bool frameSizeChanged = false;
    int frameWidth = frame.visible_rect.width;
    int frameHeight = frame.visible_rect.height;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_consecutiveCaptureFailures = 0;
        m_airanDescriptorInCurrentCapture = true;
        m_nativeDescriptorInCurrentCapture = hasNativeDescriptor;
        frameSizeChanged = updateCapturedSizeLocked(frameWidth, frameHeight, "native");
        external = m_externalCallback;
    }
    if (frameSizeChanged)
        notifyFrameSizeChanged(frameWidth, frameHeight);

    if (hasNativeDescriptor)
    {
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_currentEncodePath = frame.current_encode_path;
            m_captureFallback.setCurrentEncodePath(m_currentEncodePath);
        }
        if (frame.capture_backend.empty())
            frame.capture_backend = isD3D11GpuFrame(frame) ? "wgc" : "native_gpu";
        const auto transition = m_captureFallback.markNativeFrame();
        frame.current_capture_path = transition.current_capture_path;
        frame.current_encode_path = transition.current_encode_path;
        frame.fallback_reason = transition.fallback_reason;
        onAiranCaptureTransition(transition);
    }

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (isD3D11GpuFrame(frame))
    {
        auto *texture = static_cast<ID3D11Texture2D *>(frame.native_handle.handle);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> retainedTexture(texture);
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        retainedTexture->GetDevice(&device);
        D3D11_TEXTURE2D_DESC desc = {};
        retainedTexture->GetDesc(&desc);
        auto buffer = makeD3D11TextureFrameBuffer(std::move(device),
                                                  std::move(retainedTexture),
                                                  frame.native_handle.subresource,
                                                  frame.visible_rect.width,
                                                  frame.visible_rect.height,
                                                  desc.Format,
                                                  frame.release);
        const int64_t timestampUs = frame.timestamp.count() > 0 ? frame.timestamp.count() : currentTimeMicros();
        FrameAdaptation adaptation;
        if (!adaptFrame(frame.visible_rect.width, frame.visible_rect.height, timestampUs, &adaptation))
        {
            OnFrameDropped();
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_webrtcFrameInCurrentCapture = true;
            if (external)
                external->onAiranCaptureFrame(std::move(frame));
            return;
        }

        auto adaptedBuffer = adaptVideoBuffer(buffer, adaptation);
        if (!adaptedBuffer)
        {
            LOG_WARN("Airan desktop native frame adaptation failed: input={}x{}, output={}x{}, crop={}x{}+{},{}",
                     frame.visible_rect.width,
                     frame.visible_rect.height,
                     adaptation.outputWidth,
                     adaptation.outputHeight,
                     adaptation.cropWidth,
                     adaptation.cropHeight,
                     adaptation.cropX,
                     adaptation.cropY);
            OnFrameDropped();
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_webrtcFrameInCurrentCapture = true;
            if (external)
                external->onAiranCaptureFrame(std::move(frame));
            return;
        }

        auto videoFrame = webrtc::VideoFrame::Builder()
                              .set_video_frame_buffer(adaptedBuffer)
                              .set_timestamp_us(timestampUs)
                              .set_rotation(webrtc::kVideoRotation_0)
                              .build();
        OnFrame(videoFrame);
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_webrtcFrameInCurrentCapture = true;
        }
    }
#endif

    if (external)
        external->onAiranCaptureFrame(std::move(frame));
}

void AiranDesktopCaptureSource::onAiranCaptureTransition(const airan::media::PathTransition &transition)
{
    airan::media::AiranCaptureCallback *external = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_currentEncodePath = transition.current_encode_path;
        m_captureFallback.setCurrentEncodePath(m_currentEncodePath);
        external = m_externalCallback;
    }
    if (external)
        external->onAiranCaptureTransition(transition);
}

} // namespace rtc
