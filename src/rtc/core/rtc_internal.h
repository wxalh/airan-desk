#ifndef AIRAN_RTC_INTERNAL_H
#define AIRAN_RTC_INTERNAL_H

#include "media/capture/core/airan_capture_interface.h"
#include "rtc/core/rtc.hpp"

#include <api/rtp_parameters.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rtc
{
extern std::atomic<int> g_instanceCount;

void ensureInitialized();
std::string currentDesktopCaptureBackend();
void setDesktopCaptureBackend(std::string backend);
void resetDesktopCaptureBackend();
int parseMLineIndex(const std::string &mid);
binary bytesToBinary(const uint8_t *data, size_t size);
webrtc::RtpTransceiverDirection toNativeDirection(Description::Direction direction);
webrtc::SdpType toNativeSdpType(Description::Type type);
bool convertFrameToQtBgra(const webrtc::VideoFrame &frame, uint8_t *dst, int width, int height);
std::vector<webrtc::RtpEncodingParameters> createDesktopVideoSendEncodings(int fps,
                                                                           int width,
                                                                           int height,
                                                                           bool simulcastRequested,
                                                                           const std::string &qualityProfile = "balanced");
void configureDesktopVideoSender(const scoped_refptr<webrtc::RtpSenderInterface> &sender,
                                 int fps,
                                 int width = 0,
                                 int height = 0,
                                 bool simulcastRequested = false,
                                 const std::string &qualityProfile = "balanced");
void applyAiranVideoCodecPreferences(const scoped_refptr<webrtc::RtpTransceiverInterface> &transceiver,
                                     const scoped_refptr<webrtc::PeerConnectionFactoryInterface> &factory,
                                     bool senderCapabilities,
                                     const std::vector<VideoCodecCapability> *remoteCapabilities = nullptr,
                                     int targetWidth = 0,
                                     int targetHeight = 0,
                                     int targetFps = 0);

class DesktopVideoSource : public AdaptedVideoTrackSource
{
public:
    using FrameSizeCallback = std::function<void(int, int)>;

    DesktopVideoSource() : AdaptedVideoTrackSource(2) {}
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual bool switchSource(int sourceIndex) { (void)sourceIndex; return false; }
    virtual bool switchSourceId(intptr_t sourceId) { (void)sourceId; return false; }
    virtual bool reconfigureCaptureOptions() { return false; }
    virtual void setTargetResolution(int width, int height, int fps)
    {
        (void)width;
        (void)height;
        (void)fps;
    }
    virtual airan::media::CaptureCapabilities captureCapabilities() const
    {
        airan::media::CaptureCapabilities capabilities;
        capabilities.cpu = true;
        capabilities.preferred_capture_path = airan::media::CapturePath::WebRtcDerivedCpuCapture;
        return capabilities;
    }
    virtual void setAiranCaptureCallback(airan::media::AiranCaptureCallback *callback)
    {
        (void)callback;
    }
    virtual void setFrameSizeCallback(FrameSizeCallback callback)
    {
        (void)callback;
    }

    bool is_screencast() const override { return true; }
    bool remote() const override { return false; }
    SourceState state() const override { return m_state.load(); }
#if AIRAN_WEBRTC_MILESTONE >= 144
    std::optional<bool> needs_denoising() const override { return false; }
#else
    absl::optional<bool> needs_denoising() const override { return false; }
#endif

protected:
    void SetState(SourceState state)
    {
        if (m_state.exchange(state) != state)
            FireOnChanged();
    }

    ~DesktopVideoSource() override = default;

private:
    std::atomic<SourceState> m_state{SourceState::kInitializing};
};

} // namespace rtc

#endif /* AIRAN_RTC_INTERNAL_H */
