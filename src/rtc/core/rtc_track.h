#pragma once

#include "rtc/core/d3d11_video_frame.h"
#include "media/capture/core/airan_capture_interface.h"
#include "rtc/core/rtc_media_types.h"
#include "rtc/core/rtc_signaling_types.h"

#include <api/media_stream_interface.h>
#include <api/rtp_sender_interface.h>
#include <api/video/video_frame.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace rtc
{


class Track final : public std::enable_shared_from_this<Track>,
                    private VideoSinkInterface<webrtc::VideoFrame>,
                    private webrtc::AudioTrackSinkInterface
{
public:
    using FrameCallback = std::function<void(binary, FrameInfo)>;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    using D3D11FrameCallback = std::function<void(D3D11VideoFrame, FrameInfo)>;
#endif

    Track(scoped_refptr<DesktopVideoSource> source,
          scoped_refptr<webrtc::VideoTrackInterface> track,
          scoped_refptr<webrtc::RtpSenderInterface> sender,
          std::string mid,
          int desktopFps = 30,
          bool desktopSimulcastRequested = false,
          int desktopTargetWidth = 0,
          int desktopTargetHeight = 0,
          std::string desktopQualityProfile = "balanced",
          Thread *signalingThread = nullptr);
    Track(scoped_refptr<webrtc::AudioSourceInterface> source,
          scoped_refptr<webrtc::AudioTrackInterface> track,
          scoped_refptr<webrtc::RtpSenderInterface> sender,
          std::string mid,
          Description::Media::Direction direction = Description::Media::Direction::SendRecv);
    Track(bool isVideoPlaceholder, std::string mid, Description::Media::Direction direction);
    explicit Track(scoped_refptr<webrtc::MediaStreamTrackInterface> remoteTrack,
                   std::string mid);
    ~Track() override;

    bool isOpen() const { return m_open.load(); }
    void close();
    void resetCallbacks();
    void onFrame(FrameCallback cb);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    void onD3D11Frame(D3D11FrameCallback cb);
#endif
    void setMediaHandler(std::shared_ptr<MediaHandler>) {}
    void setEnabled(bool enabled);
    bool switchDesktopSource(int sourceIndex);
    bool switchDesktopSourceId(intptr_t sourceId);
    bool requestKeyFrame();
    bool reconfigureDesktopCaptureOptions();
    void setDesktopTargetResolution(int width, int height, int fps);
    void setDesktopQualityProfile(std::string qualityProfile);
    void setAiranCaptureCallback(airan::media::AiranCaptureCallback *callback);
    void bindDesktopSourceCallbacks();
    void captureAudioFrame(const void *audioData, int bitsPerSample, int sampleRate, size_t channels, size_t frames);
    std::string mid() const { return m_mid; }
    scoped_refptr<webrtc::RtpSenderInterface> sender() const { return m_sender; }
    bool isVideo() const { return m_isVideo; }
    int desktopFps() const { return m_desktopFps; }

    struct DescriptionView
    {
        std::string typeValue;
        std::string type() const { return typeValue; }
    };
    DescriptionView description() const { return {m_isVideo ? "video" : "audio"}; }
    Description::Media::Direction direction() const { return m_direction; }

private:
    void OnFrame(const webrtc::VideoFrame &frame) override;
    void OnData(const void *audio_data, int bits_per_sample, int sample_rate, size_t number_of_channels, size_t number_of_frames) override;
    void runOnSignalingThreadSync(std::function<void()> task);
    void onDesktopSourceFrameSizeChanged(int width, int height);
    void configureDesktopSender(int width, int height, const char *reason);

    scoped_refptr<DesktopVideoSource> m_videoSource;
    scoped_refptr<webrtc::VideoTrackInterface> m_videoTrack;
    scoped_refptr<webrtc::AudioSourceInterface> m_audioSource;
    scoped_refptr<webrtc::AudioTrackInterface> m_audioTrack;
    scoped_refptr<webrtc::MediaStreamTrackInterface> m_remoteTrack;
    scoped_refptr<webrtc::RtpSenderInterface> m_sender;
    Thread *m_signalingThread = nullptr;
    std::string m_mid;
    int m_desktopFps{30};
    int m_desktopTargetWidth{0};
    int m_desktopTargetHeight{0};
    std::string m_desktopQualityProfile{"balanced"};
    bool m_desktopSimulcastRequested{false};
    bool m_isVideo{false};
    Description::Media::Direction m_direction{Description::Media::Direction::SendRecv};
    std::atomic_bool m_open{true};
    bool m_videoSinkAttached{false};
    bool m_audioSinkAttached{false};
    std::atomic_bool m_firstFrameLogged{false};
    std::atomic_bool m_frameConvertFailureLogged{false};
    std::atomic_bool m_customAudioWarningLogged{false};
    std::mutex m_callbackMutex;
    FrameCallback m_onFrame;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    D3D11FrameCallback m_onD3D11Frame;
#endif
};

} // namespace rtc
