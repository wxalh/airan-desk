#include "rtc/core/rtc_internal.h"

#include "common/logger_manager.h"
#include "api/media_stream_interface.h"
#include "pc/rtp_sender.h"

#include <utility>

namespace rtc
{
Track::Track(scoped_refptr<DesktopVideoSource> source,
             scoped_refptr<webrtc::VideoTrackInterface> track,
             scoped_refptr<webrtc::RtpSenderInterface> sender,
             std::string mid,
             int desktopFps,
             bool desktopSimulcastRequested,
             int desktopTargetWidth,
             int desktopTargetHeight,
             std::string desktopQualityProfile,
             Thread *signalingThread)
    : m_videoSource(source), m_videoTrack(track), m_sender(sender), m_mid(std::move(mid)), m_isVideo(true),
      m_signalingThread(signalingThread),
      m_desktopFps(desktopFps), m_desktopTargetWidth(desktopTargetWidth), m_desktopTargetHeight(desktopTargetHeight),
      m_desktopQualityProfile(std::move(desktopQualityProfile)),
      m_desktopSimulcastRequested(desktopSimulcastRequested),
      m_direction(Description::Media::Direction::SendOnly)
{
    LOG_DEBUG("Video track wrapper created: mid={}, hasSource={}, hasTrack={}, hasSender={}, fps={}, qualityProfile={}, simulcastRequested={}",
              m_mid,
              m_videoSource.get() != nullptr,
              m_videoTrack.get() != nullptr,
              m_sender.get() != nullptr,
              m_desktopFps,
              m_desktopQualityProfile,
              m_desktopSimulcastRequested);
}

Track::Track(scoped_refptr<webrtc::AudioSourceInterface> source,
             scoped_refptr<webrtc::AudioTrackInterface> track,
             scoped_refptr<webrtc::RtpSenderInterface> sender,
             std::string mid,
             Description::Media::Direction direction)
    : m_audioSource(source), m_audioTrack(track), m_sender(sender), m_mid(std::move(mid)), m_isVideo(false),
      m_direction(direction)
{
    LOG_DEBUG("Audio track wrapper created: mid={}, hasSource={}, hasTrack={}, hasSender={}, direction={}",
              m_mid,
              m_audioSource.get() != nullptr,
              m_audioTrack.get() != nullptr,
              m_sender.get() != nullptr,
              static_cast<int>(m_direction));
}

Track::Track(bool isVideoPlaceholder, std::string mid, Description::Media::Direction direction)
    : m_mid(std::move(mid)), m_isVideo(isVideoPlaceholder), m_direction(direction)
{
    LOG_DEBUG("Placeholder track wrapper created: mid={}, type={}, direction={}",
              m_mid, m_isVideo ? "video" : "audio", static_cast<int>(m_direction));
}

Track::Track(scoped_refptr<webrtc::MediaStreamTrackInterface> remoteTrack, std::string mid)
    : m_remoteTrack(remoteTrack), m_mid(std::move(mid))
{
    const std::string kind = remoteTrack ? remoteTrack->kind() : std::string();
    m_isVideo = kind == webrtc::MediaStreamTrackInterface::kVideoKind;
    LOG_DEBUG("Remote track wrapper created: mid={}, kind={}, hasTrack={}", m_mid, kind, remoteTrack.get() != nullptr);
    if (m_isVideo)
    {
        m_videoTrack = scoped_refptr<webrtc::VideoTrackInterface>(
            static_cast<webrtc::VideoTrackInterface *>(remoteTrack.get()));
        if (m_videoTrack)
        {
            m_videoTrack->AddOrUpdateSink(this, VideoSinkWants());
            m_videoSinkAttached = true;
        }
    }
    else
    {
        m_audioTrack = scoped_refptr<webrtc::AudioTrackInterface>(
            static_cast<webrtc::AudioTrackInterface *>(remoteTrack.get()));
        if (m_audioTrack)
        {
            m_audioTrack->AddSink(this);
            m_audioSinkAttached = true;
        }
    }
}

Track::~Track()
{
    close();
}

void Track::runOnSignalingThreadSync(std::function<void()> task)
{
    if (!task)
        return;
    if (m_signalingThread && !m_signalingThread->IsQuitting() && !m_signalingThread->IsCurrent())
    {
        m_signalingThread->BlockingCall([&task]() {
            task();
        });
        return;
    }
    task();
}

void Track::close()
{
    if (!m_open.exchange(false))
        return;
    runOnSignalingThreadSync([this]() {
        if (m_videoSource)
            m_videoSource->setFrameSizeCallback(nullptr);
        if (m_videoTrack && m_videoSinkAttached)
        {
            m_videoTrack->RemoveSink(this);
            m_videoSinkAttached = false;
        }
        if (m_audioTrack && m_audioSinkAttached)
        {
            m_audioTrack->RemoveSink(this);
            m_audioSinkAttached = false;
        }
        if (m_videoSource)
            m_videoSource->stop();
    });
    LOG_DEBUG("Track closed: mid={}, type={}", m_mid, m_isVideo ? "video" : "audio");
}

void Track::resetCallbacks()
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onFrame = nullptr;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    m_onD3D11Frame = nullptr;
#endif
}

void Track::onFrame(FrameCallback cb)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onFrame = std::move(cb);
}

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
void Track::onD3D11Frame(D3D11FrameCallback cb)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onD3D11Frame = std::move(cb);
}
#endif

void Track::setEnabled(bool enabled)
{
    runOnSignalingThreadSync([this, enabled]() {
        if (m_videoTrack)
            m_videoTrack->set_enabled(enabled);
        if (m_audioTrack)
            m_audioTrack->set_enabled(enabled);
        if (m_videoSource)
        {
            if (enabled)
            {
                if (!m_videoSource->start())
                    LOG_WARN("Video source failed to start: mid={}", m_mid);
            }
            else
            {
                m_videoSource->stop();
            }
        }
    });
    LOG_DEBUG("Track enabled state changed: mid={}, type={}, enabled={}", m_mid, m_isVideo ? "video" : "audio", enabled);
}

bool Track::switchDesktopSource(int sourceIndex)
{
    if (!m_videoSource)
    {
        LOG_WARN("Cannot switch desktop source: mid={}, no video source", m_mid);
        return false;
    }

    const bool switched = m_videoSource->switchSource(sourceIndex);
    LOG_INFO("Desktop source switch requested: mid={}, sourceIndex={}, result={}",
             m_mid, sourceIndex, switched ? "ok" : "failed");
    return switched;
}

bool Track::switchDesktopSourceId(intptr_t sourceId)
{
    if (!m_videoSource)
    {
        LOG_WARN("Cannot switch desktop source: mid={}, no video source", m_mid);
        return false;
    }

    const bool switched = m_videoSource->switchSourceId(sourceId);
    LOG_INFO("Desktop source id switch requested: mid={}, sourceId={}, result={}",
             m_mid, sourceId, switched ? "ok" : "failed");
    return switched;
}

bool Track::requestKeyFrame()
{
    if (!m_sender && !m_videoTrack)
    {
        LOG_WARN("Cannot request keyframe: mid={}, no video sender or track", m_mid);
        return false;
    }

    auto request = [sender = m_sender, track = m_videoTrack, mid = m_mid]() {
        if (sender)
        {
            if (auto *videoSender = dynamic_cast<webrtc::VideoRtpSender *>(sender.get()))
            {
                const auto result = videoSender->GenerateKeyFrame({});
                if (result.ok())
                {
                    LOG_DEBUG("Desktop keyframe requested via sender: mid={}", mid);
                    return;
                }
                LOG_WARN("Desktop keyframe request via sender failed: mid={}, error={}", mid, result.message());
            }
            else
            {
                LOG_WARN("Desktop keyframe request skipped: mid={}, sender does not support video keyframe generation", mid);
            }
        }

        auto *source = track ? track->GetSource() : nullptr;
        if (!source)
        {
            LOG_WARN("Desktop keyframe request skipped: mid={}, no video source", mid);
            return;
        }
        source->GenerateKeyFrame();
        LOG_DEBUG("Desktop keyframe requested via source fallback: mid={}", mid);
    };

    if (m_signalingThread && !m_signalingThread->IsQuitting())
    {
        if (m_signalingThread->IsCurrent())
            request();
        else
            m_signalingThread->BlockingCall([&request]() { request(); });
        return true;
    }

    request();
    return true;
}

bool Track::reconfigureDesktopCaptureOptions()
{
    if (!m_videoSource)
    {
        LOG_WARN("Cannot reconfigure desktop capture options: mid={}, no video source", m_mid);
        return false;
    }

    const bool reconfigured = m_videoSource->reconfigureCaptureOptions();
    LOG_DEBUG("Desktop capture options reconfigure requested: mid={}, result={}",
              m_mid, reconfigured ? "ok" : "failed");
    return reconfigured;
}

void Track::setAiranCaptureCallback(airan::media::AiranCaptureCallback *callback)
{
    if (m_videoSource)
        m_videoSource->setAiranCaptureCallback(callback);
}

void Track::bindDesktopSourceCallbacks()
{
    if (!m_videoSource)
        return;
    std::weak_ptr<Track> weakSelf = weak_from_this();
    m_videoSource->setFrameSizeCallback([weakSelf](int width, int height) {
        if (auto self = weakSelf.lock())
            self->onDesktopSourceFrameSizeChanged(width, height);
    });
}

void Track::setDesktopTargetResolution(int width, int height, int fps)
{
    m_desktopFps = fps > 0 ? fps : m_desktopFps;
    m_desktopTargetWidth = width > 0 ? width : 0;
    m_desktopTargetHeight = height > 0 ? height : 0;
    if (m_videoSource)
        m_videoSource->setTargetResolution(width, height, m_desktopFps);
    configureDesktopSender(width, height, "target-resolution");
    LOG_DEBUG("Desktop target resolution updated: mid={}, target={}x{}, fps={}",
              m_mid,
              width,
              height,
              m_desktopFps);
}

void Track::setDesktopQualityProfile(std::string qualityProfile)
{
    if (qualityProfile.empty())
        qualityProfile = "balanced";
    if (m_desktopQualityProfile == qualityProfile)
        return;

    m_desktopQualityProfile = std::move(qualityProfile);
    const int targetWidth = m_desktopTargetWidth > 0 ? m_desktopTargetWidth : 0;
    const int targetHeight = m_desktopTargetHeight > 0 ? m_desktopTargetHeight : 0;
    configureDesktopSender(targetWidth, targetHeight, "quality-profile");
    LOG_INFO("Desktop quality profile updated: mid={}, qualityProfile={}",
             m_mid,
             m_desktopQualityProfile);
}

void Track::onDesktopSourceFrameSizeChanged(int width, int height)
{
    if (!m_open.load() || !m_sender)
        return;
    const int targetWidth = m_desktopTargetWidth > 0 ? m_desktopTargetWidth : width;
    const int targetHeight = m_desktopTargetHeight > 0 ? m_desktopTargetHeight : height;
    configureDesktopSender(targetWidth, targetHeight, "source-frame-size");
    LOG_DEBUG("Desktop source frame size changed: mid={}, source={}x{}, senderTarget={}x{}, fps={}",
              m_mid,
              width,
              height,
              targetWidth,
              targetHeight,
              m_desktopFps);
}

void Track::configureDesktopSender(int width, int height, const char *reason)
{
    if (!m_sender)
        return;

    auto sender = m_sender;
    const int fps = m_desktopFps;
    const bool simulcastRequested = m_desktopSimulcastRequested;
    const std::string qualityProfile = m_desktopQualityProfile;
    const std::string mid = m_mid;
    const std::string safeReason = reason ? reason : "unknown";
    auto configure = [sender, fps, width, height, simulcastRequested, qualityProfile, mid, safeReason]() {
        configureDesktopVideoSender(sender, fps, width, height, simulcastRequested, qualityProfile);
        LOG_DEBUG("Desktop sender configured on WebRTC thread: mid={}, reason={}, target={}x{}, fps={}, qualityProfile={}",
                  mid,
                  safeReason,
                  width,
                  height,
                  fps,
                  qualityProfile);
    };

    if (m_signalingThread && !m_signalingThread->IsQuitting())
    {
        if (m_signalingThread->IsCurrent())
            configure();
        else
            m_signalingThread->BlockingCall([&configure]() {
                configure();
            });
        return;
    }

    configure();
}

} // namespace rtc
