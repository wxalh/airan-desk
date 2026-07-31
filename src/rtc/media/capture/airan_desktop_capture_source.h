#pragma once

#include "media/capture/core/airan_capture_interface.h"
#include "rtc/core/rtc_internal.h"

#include "desktop_capture/desktop_capturer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <QString>
#include <thread>

namespace rtc
{

bool isAiranDesktopCaptureSourceAvailable();
scoped_refptr<DesktopVideoSource> createAiranDesktopCaptureSource(const Description::Video &desc);

class AiranDesktopCaptureSource : public DesktopVideoSource,
                                  private airan::desktop_capture::DesktopCapturer::Callback,
                                  private airan::media::AiranCaptureCallback
{
public:
    explicit AiranDesktopCaptureSource(const Description::Video &desc);
    ~AiranDesktopCaptureSource() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;
    bool switchSource(int sourceIndex) override;
    bool switchSourceId(intptr_t sourceId) override;
    bool reconfigureCaptureOptions() override;
    bool GetStats(Stats *stats) override;
    airan::media::CaptureCapabilities captureCapabilities() const override;
    void setAiranCaptureCallback(airan::media::AiranCaptureCallback *callback) override;
    void setFrameSizeCallback(FrameSizeCallback callback) override;

protected:
    void setTargetResolution(int width, int height, int fps) override;

private:
    struct FrameAdaptation
    {
        int outputWidth = 0;
        int outputHeight = 0;
        int cropWidth = 0;
        int cropHeight = 0;
        int cropX = 0;
        int cropY = 0;
    };

    void captureLoop();
    bool createCapturerOnCaptureThread();
    void destroyCapturerOnCaptureThread();
    bool selectSourceLocked();
    bool adaptFrame(int width, int height, int64_t timestampUs, FrameAdaptation *adaptation);
    scoped_refptr<webrtc::VideoFrameBuffer> adaptVideoBuffer(
        const scoped_refptr<webrtc::VideoFrameBuffer> &source,
        const FrameAdaptation &adaptation);
    void configureOutputFormatLocked();
    void emitCpuFrameDescriptor(const airan::desktop_capture::DesktopFrame &frame,
                                int64_t timestampUs,
                                const airan::media::PathTransition &transition);
    void broadcastCpuFrame(const airan::desktop_capture::DesktopFrame &frame, int64_t timestampUs);
    bool tryCaptureSecureDesktopFrame();
    bool updateCapturedSizeLocked(int width, int height, const char *label);
    airan::media::EncodePath currentEncodePathLocked() const;
    void notifyFrameSizeChanged(int width, int height);
    bool shouldTrySecureDesktopFrame();
    void markSecureDesktopCaptureFailed(const char *reason);
    void markSecureDesktopCaptureSucceeded();
    bool shouldSuppressLockedPlaceholderFrame(const airan::desktop_capture::DesktopFrame &frame);
#if defined(WEBRTC_WIN)
    std::optional<airan::desktop_capture::DesktopRect> selectedSourceRectLocked() const;
#endif

    void OnCaptureResult(airan::desktop_capture::DesktopCapturer::Result result,
                         std::unique_ptr<airan::desktop_capture::DesktopFrame> frame) override;
    void onAiranCaptureFrame(airan::media::CaptureFrameDescriptor frame) override;
    void onAiranCaptureTransition(const airan::media::PathTransition &transition) override;

    int m_sourceIndex = 0;
    intptr_t m_sourceId = 0;
    bool m_hasSourceId = false;
    int m_fps = 30;
    int m_targetWidth = 0;
    int m_targetHeight = 0;
    std::atomic_bool m_running{false};
    std::atomic_bool m_recreateCapturerRequested{false};
    std::thread m_captureThread;
    mutable std::recursive_mutex m_mutex;
    std::unique_ptr<airan::desktop_capture::DesktopCapturer> m_capturer;
    airan::media::AiranCaptureCallback *m_externalCallback = nullptr;
    FrameSizeCallback m_frameSizeCallback;
    airan::media::CaptureFallbackStateMachine m_captureFallback;
    int m_width = 0;
    int m_height = 0;
    bool m_airanDescriptorInCurrentCapture = false;
    bool m_nativeDescriptorInCurrentCapture = false;
    bool m_webrtcFrameInCurrentCapture = false;
    int m_consecutiveCaptureFailures = 0;
    airan::media::EncodePath m_currentEncodePath = airan::media::EncodePath::CpuSoftwareEncode;
    std::chrono::steady_clock::time_point m_nextSecureDesktopAttempt{};
    bool m_secureDesktopCaptureAvailable = false;
    bool m_secureDesktopFailureLogged = false;
    bool m_secureDesktopAttemptLogged = false;
    bool m_secureDesktopLowScoreLogged = false;
    bool m_lockedPlaceholderSuppressionLogged = false;
    QString m_lastSecureDesktopFrameHash;
};

} // namespace rtc
