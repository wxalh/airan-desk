#include "media/capture/core/airan_capture_interface.h"

namespace airan::media
{

CaptureFallbackStateMachine::CaptureFallbackStateMachine(FallbackPolicyConfig config)
    : m_config(config)
{
}

CapturePath CaptureFallbackStateMachine::currentPath() const
{
    return m_path;
}

bool CaptureFallbackStateMachine::breakerOpen() const
{
    return m_breakerOpen;
}

int CaptureFallbackStateMachine::failureCount() const
{
    return m_failures;
}

void CaptureFallbackStateMachine::setCurrentEncodePath(EncodePath path)
{
    m_encodePath = path;
}

PathTransition CaptureFallbackStateMachine::markNativeFrame()
{
    if (m_path == CapturePath::CaptureReprobe)
        return markReprobeFrame();

    m_failures = 0;
    m_breakerOpen = false;
    m_reprobeSuccess = 0;
    return transitionTo(CapturePath::NativeGpuCapture, FallbackReason::kNone);
}

PathTransition CaptureFallbackStateMachine::markCpuFrame(FallbackReason reason)
{
    return transitionTo(CapturePath::WebRtcDerivedCpuCapture, reason);
}

PathTransition CaptureFallbackStateMachine::markFailure(FallbackReason reason)
{
    ++m_failures;
    if (m_failures >= m_config.failure_threshold || reason == FallbackReason::DeviceError ||
        reason == FallbackReason::PermissionDenied)
    {
        m_breakerOpen = true;
        m_breakerOpenedAt = std::chrono::steady_clock::now();
        const bool thresholdTriggered = m_failures >= m_config.failure_threshold;
        const bool deviceEvent = reason == FallbackReason::DeviceError;
        const auto transitionReason =
            thresholdTriggered && !deviceEvent && reason != FallbackReason::PermissionDenied
                ? FallbackReason::CaptureFailureThreshold
                : (reason == FallbackReason::kNone ? FallbackReason::CaptureError : reason);
        auto transition = transitionTo(CapturePath::WebRtcDerivedCpuCapture, transitionReason);
        transition.threshold_triggered = thresholdTriggered;
        transition.device_event = reason == FallbackReason::DeviceError;
        return transition;
    }
    return transitionTo(m_path, reason == FallbackReason::kNone ? FallbackReason::CaptureError : reason);
}

PathTransition CaptureFallbackStateMachine::markSourceSwitch()
{
    m_failures = 0;
    m_breakerOpen = false;
    m_reprobeSuccess = 0;
    auto transition = transitionTo(CapturePath::CaptureReprobe, FallbackReason::ScreenSwitch);
    transition.screen_switch = true;
    return transition;
}

PathTransition CaptureFallbackStateMachine::beginReprobe(FallbackReason reason)
{
    if (m_breakerOpen && std::chrono::steady_clock::now() - m_breakerOpenedAt < m_config.hold_time)
        return transitionTo(m_path, reason);
    m_reprobeSuccess = 0;
    m_previousStablePath = m_path;
    return transitionTo(CapturePath::CaptureReprobe, reason);
}

PathTransition CaptureFallbackStateMachine::markReprobeFrame()
{
    if (m_path != CapturePath::CaptureReprobe)
        return transitionTo(m_path, FallbackReason::kNone);
    ++m_reprobeSuccess;
    if (m_reprobeSuccess >= m_config.successful_reprobe_frames)
    {
        m_failures = 0;
        m_breakerOpen = false;
        return transitionTo(CapturePath::NativeGpuCapture, FallbackReason::kNone);
    }
    return transitionTo(m_path, FallbackReason::kNone);
}

PathTransition CaptureFallbackStateMachine::transitionTo(CapturePath next, FallbackReason reason)
{
    PathTransition transition;
    transition.previous_capture_path = m_path;
    transition.current_capture_path = next;
    transition.previous_encode_path = m_encodePath;
    transition.current_encode_path = m_encodePath;
    transition.fallback_reason = reason;
    transition.capture_breaker_open = m_breakerOpen;
    transition.failure_count = m_failures;
    if (next != CapturePath::CaptureReprobe)
        m_previousStablePath = next;
    m_path = next;
    return transition;
}

EncodeFallbackStateMachine::EncodeFallbackStateMachine(FallbackPolicyConfig config)
    : m_config(config)
{
}

EncodePath EncodeFallbackStateMachine::currentPath() const
{
    return m_path;
}

bool EncodeFallbackStateMachine::breakerOpen() const
{
    return m_breakerOpen;
}

int EncodeFallbackStateMachine::failureCount() const
{
    return m_failures;
}

void EncodeFallbackStateMachine::setCurrentCapturePath(CapturePath path)
{
    m_capturePath = path;
}

PathTransition EncodeFallbackStateMachine::markEncodeSuccess(EncodePath path)
{
    if (m_path == EncodePath::EncodeReprobe)
        return markReprobeFrame(path);
    m_failures = 0;
    return transitionTo(path, FallbackReason::kNone);
}

PathTransition EncodeFallbackStateMachine::markEncodeFailure(FallbackReason reason, EncodePath degradedPath)
{
    ++m_failures;
    if (m_failures >= m_config.failure_threshold || reason == FallbackReason::DeviceError)
    {
        m_breakerOpen = true;
        m_breakerOpenedAt = std::chrono::steady_clock::now();
        const bool thresholdTriggered = m_failures >= m_config.failure_threshold;
        const bool deviceEvent = reason == FallbackReason::DeviceError;
        const auto transitionReason =
            thresholdTriggered && !deviceEvent
                ? FallbackReason::EncodeFailureThreshold
                : (reason == FallbackReason::kNone ? FallbackReason::EncodeFailureThreshold : reason);
        auto transition = transitionTo(degradedPath, transitionReason);
        transition.threshold_triggered = thresholdTriggered;
        transition.device_event = deviceEvent;
        return transition;
    }
    return transitionTo(degradedPath,
                        reason == FallbackReason::kNone ? FallbackReason::EncodeFailureThreshold : reason);
}

PathTransition EncodeFallbackStateMachine::markEncoderRebuild()
{
    m_failures = 0;
    m_breakerOpen = false;
    m_reprobeSuccess = 0;
    auto transition = transitionTo(EncodePath::EncodeReprobe, FallbackReason::EncoderRebuild);
    transition.encoder_rebuild = true;
    return transition;
}

PathTransition EncodeFallbackStateMachine::beginReprobe(FallbackReason reason)
{
    if (m_breakerOpen && std::chrono::steady_clock::now() - m_breakerOpenedAt < m_config.hold_time)
        return transitionTo(m_path, reason);
    m_reprobeSuccess = 0;
    m_previousStablePath = m_path;
    return transitionTo(EncodePath::EncodeReprobe, reason);
}

PathTransition EncodeFallbackStateMachine::markReprobeFrame(EncodePath promotedPath)
{
    if (m_path != EncodePath::EncodeReprobe)
        return transitionTo(m_path, FallbackReason::kNone);
    ++m_reprobeSuccess;
    if (m_reprobeSuccess >= m_config.successful_reprobe_frames)
    {
        m_failures = 0;
        m_breakerOpen = false;
        return transitionTo(promotedPath, FallbackReason::kNone);
    }
    return transitionTo(m_path, FallbackReason::kNone);
}

PathTransition EncodeFallbackStateMachine::transitionTo(EncodePath next, FallbackReason reason)
{
    PathTransition transition;
    transition.previous_capture_path = m_capturePath;
    transition.current_capture_path = m_capturePath;
    transition.previous_encode_path = m_path;
    transition.current_encode_path = next;
    transition.fallback_reason = reason;
    transition.encode_breaker_open = m_breakerOpen;
    transition.failure_count = m_failures;
    if (next != EncodePath::EncodeReprobe)
        m_previousStablePath = next;
    m_path = next;
    return transition;
}

} // namespace airan::media
