#include "media/capture/core/airan_capture_frame.h"

#include <utility>

namespace airan::media
{

FrameRelease::FrameRelease(std::function<void()> callback)
    : m_callback(std::move(callback))
{
}

FrameRelease::~FrameRelease()
{
    release();
}

void FrameRelease::release()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_released)
        return;
    m_released = true;
    if (m_callback)
    {
        m_callback();
        m_callback = {};
    }
}

bool FrameRelease::released() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_released;
}

bool CaptureFrameDescriptor::isNativeGpu() const
{
    return frame_kind == FrameKind::NativeGpu && native_handle.type != NativeHandleType::kNone;
}

bool CaptureFrameDescriptor::hasMandatoryRelease() const
{
    return static_cast<bool>(release);
}

std::shared_ptr<FrameRelease> makeFrameRelease(std::function<void()> callback)
{
    return std::make_shared<FrameRelease>(std::move(callback));
}

CaptureFrameDescriptor makeReleasedFrameDescriptor(CaptureFrameDescriptor descriptor,
                                                   std::function<void()> release)
{
    descriptor.release = makeFrameRelease(std::move(release));
    return descriptor;
}

const char *toString(FrameKind value)
{
    switch (value)
    {
    case FrameKind::NativeGpu: return "NativeGpu";
    case FrameKind::CpuBgra: return "CpuBgra";
    case FrameKind::CpuArgb: return "CpuArgb";
    case FrameKind::CpuI420: return "CpuI420";
    case FrameKind::CpuNV12: return "CpuNV12";
    }
    return "Unknown";
}

const char *toString(NativeHandleType value)
{
    switch (value)
    {
    case NativeHandleType::kNone: return "None";
    case NativeHandleType::D3D11Texture2D: return "D3D11Texture2D";
    case NativeHandleType::DmaBuf: return "DmaBuf";
    case NativeHandleType::CVPixelBuffer: return "CVPixelBuffer";
    case NativeHandleType::IOSurface: return "IOSurface";
    }
    return "Unknown";
}

const char *toString(CapturePath value)
{
    switch (value)
    {
    case CapturePath::NativeGpuCapture: return "NativeGpuCapture";
    case CapturePath::WebRtcDerivedCpuCapture: return "WebRtcDerivedCpuCapture";
    case CapturePath::CaptureReprobe: return "CaptureReprobe";
    }
    return "Unknown";
}

const char *toString(EncodePath value)
{
    switch (value)
    {
    case EncodePath::GpuZeroCopyEncode: return "GpuZeroCopyEncode";
    case EncodePath::GpuCopyHwEncode: return "GpuCopyHwEncode";
    case EncodePath::CpuUploadHwEncode: return "CpuUploadHwEncode";
    case EncodePath::CpuReadbackHwEncode: return "CpuReadbackHwEncode";
    case EncodePath::CpuSoftwareEncode: return "CpuSoftwareEncode";
    case EncodePath::EncodeReprobe: return "EncodeReprobe";
    }
    return "Unknown";
}

const char *toString(FallbackReason value)
{
    switch (value)
    {
    case FallbackReason::kNone: return "None";
    case FallbackReason::CapabilityMissing: return "CapabilityMissing";
    case FallbackReason::HandleIncompatible: return "HandleIncompatible";
    case FallbackReason::DeviceMismatch: return "DeviceMismatch";
    case FallbackReason::SyncUnsupported: return "SyncUnsupported";
    case FallbackReason::CaptureFailureThreshold: return "CaptureFailureThreshold";
    case FallbackReason::EncodeFailureThreshold: return "EncodeFailureThreshold";
    case FallbackReason::DeviceError: return "DeviceError";
    case FallbackReason::CaptureError: return "CaptureError";
    case FallbackReason::PermissionDenied: return "PermissionDenied";
    case FallbackReason::ScreenSwitch: return "ScreenSwitch";
    case FallbackReason::EncoderRebuild: return "EncoderRebuild";
    case FallbackReason::PipelineReinit: return "PipelineReinit";
    }
    return "Unknown";
}

} // namespace airan::media
