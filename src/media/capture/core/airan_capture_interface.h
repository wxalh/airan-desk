#pragma once

#include "media/capture/core/airan_capture_frame.h"

#include <chrono>
#include <vector>

namespace airan::media
{

struct CaptureCapabilities
{
    bool native_gpu = false;
    bool cpu = false;
    std::vector<NativeHandleType> native_handle_types;
    CapturePath preferred_capture_path = CapturePath::WebRtcDerivedCpuCapture;
};

struct PathTransition
{
    FallbackReason fallback_reason = FallbackReason::kNone;
    CapturePath previous_capture_path = CapturePath::WebRtcDerivedCpuCapture;
    CapturePath current_capture_path = CapturePath::WebRtcDerivedCpuCapture;
    EncodePath previous_encode_path = EncodePath::CpuSoftwareEncode;
    EncodePath current_encode_path = EncodePath::CpuSoftwareEncode;
    bool capture_breaker_open = false;
    bool encode_breaker_open = false;
    int failure_count = 0;
    bool threshold_triggered = false;
    bool device_event = false;
    bool screen_switch = false;
    bool encoder_rebuild = false;
    bool pipeline_reinit = false;
};

class AiranCaptureCallback
{
public:
    virtual ~AiranCaptureCallback() = default;
    virtual void onAiranCaptureFrame(CaptureFrameDescriptor frame) = 0;
    virtual void onAiranCaptureTransition(const PathTransition &transition) = 0;
};

struct FallbackPolicyConfig
{
    int failure_threshold = 3;
    int successful_reprobe_frames = 3;
    std::chrono::milliseconds hold_time{3000};
};

class CaptureFallbackStateMachine
{
public:
    explicit CaptureFallbackStateMachine(FallbackPolicyConfig config = {});

    CapturePath currentPath() const;
    bool breakerOpen() const;
    int failureCount() const;
    void setCurrentEncodePath(EncodePath path);
    PathTransition markNativeFrame();
    PathTransition markCpuFrame(FallbackReason reason);
    PathTransition markFailure(FallbackReason reason);
    PathTransition markSourceSwitch();
    PathTransition beginReprobe(FallbackReason reason);
    PathTransition markReprobeFrame();

private:
    PathTransition transitionTo(CapturePath next, FallbackReason reason);

    FallbackPolicyConfig m_config;
    CapturePath m_path = CapturePath::NativeGpuCapture;
    EncodePath m_encodePath = EncodePath::GpuZeroCopyEncode;
    CapturePath m_previousStablePath = CapturePath::NativeGpuCapture;
    int m_failures = 0;
    int m_reprobeSuccess = 0;
    bool m_breakerOpen = false;
    std::chrono::steady_clock::time_point m_breakerOpenedAt{};
};

class EncodeFallbackStateMachine
{
public:
    explicit EncodeFallbackStateMachine(FallbackPolicyConfig config = {});

    EncodePath currentPath() const;
    bool breakerOpen() const;
    int failureCount() const;
    void setCurrentCapturePath(CapturePath path);
    PathTransition markEncodeSuccess(EncodePath path);
    PathTransition markEncodeFailure(FallbackReason reason, EncodePath degradedPath);
    PathTransition markEncoderRebuild();
    PathTransition beginReprobe(FallbackReason reason);
    PathTransition markReprobeFrame(EncodePath promotedPath);

private:
    PathTransition transitionTo(EncodePath next, FallbackReason reason);

    FallbackPolicyConfig m_config;
    CapturePath m_capturePath = CapturePath::NativeGpuCapture;
    EncodePath m_path = EncodePath::GpuZeroCopyEncode;
    EncodePath m_previousStablePath = EncodePath::GpuZeroCopyEncode;
    int m_failures = 0;
    int m_reprobeSuccess = 0;
    bool m_breakerOpen = false;
    std::chrono::steady_clock::time_point m_breakerOpenedAt{};
};

} // namespace airan::media
