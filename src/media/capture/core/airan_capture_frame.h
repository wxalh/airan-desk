#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace airan::media
{

enum class FrameKind
{
    NativeGpu,
    CpuBgra,
    CpuArgb,
    CpuI420,
    CpuNV12,
};

enum class NativeHandleType
{
    kNone,
    D3D11Texture2D,
    DmaBuf,
    CVPixelBuffer,
    IOSurface,
};

enum class NativeHandleOwnership
{
    Borrowed,
    Retained,
};

enum class SyncType
{
    kNone,
    Fence,
    KeyedMutex,
    Event,
    ImplicitSync,
};

enum class SyncWaiter
{
    kNone,
    Producer,
    Consumer,
};

enum class CapturePath
{
    NativeGpuCapture,
    WebRtcDerivedCpuCapture,
    CaptureReprobe,
};

enum class EncodePath
{
    GpuZeroCopyEncode,
    GpuCopyHwEncode,
    CpuUploadHwEncode,
    CpuReadbackHwEncode,
    CpuSoftwareEncode,
    EncodeReprobe,
};

enum class FallbackReason
{
    kNone,
    CapabilityMissing,
    HandleIncompatible,
    DeviceMismatch,
    SyncUnsupported,
    CaptureFailureThreshold,
    EncodeFailureThreshold,
    DeviceError,
    CaptureError,
    PermissionDenied,
    ScreenSwitch,
    EncoderRebuild,
    PipelineReinit,
};

struct FrameRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct FrameSize
{
    int width = 0;
    int height = 0;
};

struct NativeHandleDescriptor
{
    NativeHandleType type = NativeHandleType::kNone;
    void *handle = nullptr;
    NativeHandleOwnership ownership = NativeHandleOwnership::Borrowed;
    std::shared_ptr<void> retained_owner;
    uint32_t subresource = 0;
};

struct DeviceIdentity
{
    std::string platform;
    std::string id;
    uint64_t luid = 0;
};

struct SyncDescriptor
{
    SyncType type = SyncType::kNone;
    SyncWaiter waiter = SyncWaiter::kNone;
    void *handle = nullptr;
};

struct PlaneDescriptor
{
    void *data = nullptr;
    int stride = 0;
    int offset = 0;
    int width = 0;
    int height = 0;
    uint32_t native_plane = 0;
};

enum class ColorValue
{
    Unknown,
    Bt601,
    Bt709,
    Bt2020,
    Srgb,
};

enum class ColorRange
{
    Unknown,
    Full,
    Limited,
};

struct ColorSpaceDescriptor
{
    ColorValue primaries = ColorValue::Unknown;
    ColorValue transfer = ColorValue::Unknown;
    ColorValue matrix = ColorValue::Unknown;
    ColorRange range = ColorRange::Unknown;
};

class FrameRelease
{
public:
    explicit FrameRelease(std::function<void()> callback);
    FrameRelease(const FrameRelease &) = delete;
    FrameRelease &operator=(const FrameRelease &) = delete;
    ~FrameRelease();

    void release();
    bool released() const;

private:
    std::function<void()> m_callback;
    mutable std::mutex m_mutex;
    bool m_released = false;
};

struct CaptureFrameDescriptor
{
    FrameKind frame_kind = FrameKind::CpuBgra;
    std::string capture_backend;
    NativeHandleDescriptor native_handle;
    DeviceIdentity device_id;
    SyncDescriptor sync;
    FrameRect visible_rect;
    FrameSize coded_size;
    std::vector<PlaneDescriptor> planes;
    ColorSpaceDescriptor color_space;
    std::chrono::microseconds timestamp{0};
    std::shared_ptr<FrameRelease> release;
    CapturePath current_capture_path = CapturePath::WebRtcDerivedCpuCapture;
    EncodePath current_encode_path = EncodePath::CpuSoftwareEncode;
    FallbackReason fallback_reason = FallbackReason::kNone;

    bool isNativeGpu() const;
    bool hasMandatoryRelease() const;
};

std::shared_ptr<FrameRelease> makeFrameRelease(std::function<void()> callback);
CaptureFrameDescriptor makeReleasedFrameDescriptor(CaptureFrameDescriptor descriptor,
                                                   std::function<void()> release);

const char *toString(FrameKind value);
const char *toString(NativeHandleType value);
const char *toString(CapturePath value);
const char *toString(EncodePath value);
const char *toString(FallbackReason value);

} // namespace airan::media
