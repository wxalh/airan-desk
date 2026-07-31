#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_probe.h"

namespace airan::media::ffmpeg
{

#if defined(AIRAN_HAVE_FFMPEG)
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
constexpr AVHWDeviceType kNativeD3D11Device = AV_HWDEVICE_TYPE_D3D11VA;
constexpr AVPixelFormat kNativeD3D11Format = AV_PIX_FMT_D3D11;
#else
constexpr AVHWDeviceType kNativeD3D11Device = AV_HWDEVICE_TYPE_NONE;
constexpr AVPixelFormat kNativeD3D11Format = AV_PIX_FMT_NONE;
#endif
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#define AIRAN_FFMPEG_NATIVE_HW_DECODER 1
constexpr AVHWDeviceType kNativeDecoderDevice = AV_HWDEVICE_TYPE_D3D11VA;
constexpr AVPixelFormat kNativeDecoderFormat = AV_PIX_FMT_D3D11;
constexpr const char *kNativeDecoderZeroCopy = "d3d11-texture/d3d11va";
#elif defined(__linux__)
#define AIRAN_FFMPEG_NATIVE_HW_DECODER 1
constexpr AVHWDeviceType kNativeDecoderDevice = AV_HWDEVICE_TYPE_VAAPI;
constexpr AVPixelFormat kNativeDecoderFormat = AV_PIX_FMT_VAAPI;
constexpr const char *kNativeDecoderZeroCopy = "vaapi/dmabuf";
#endif
#endif


#if defined(AIRAN_HAVE_FFMPEG)
constexpr CodecProbe kCodecProbes[] = {
#if defined(AIRAN_FFMPEG_NATIVE_HW_DECODER)
    {CodecKind::H264, "h264", "h264", kNativeDecoderZeroCopy, false, true, kNativeDecoderDevice, kNativeDecoderFormat, AV_PIX_FMT_NV12},
#endif
    {CodecKind::H264, "h264_nvenc", "h264_nvenc", "d3d11-texture/nvenc", true, false, kNativeD3D11Device, kNativeD3D11Format, AV_PIX_FMT_NV12},
    {CodecKind::H264, "h264_qsv", "h264_qsv", "qsv/d3d11", true, true, AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, AV_PIX_FMT_NV12},
#if defined(AIRAN_FFMPEG_HAVE_AMF)
    {CodecKind::H264, "h264_amf", "h264_amf", "amf-surface", true, true, AV_HWDEVICE_TYPE_AMF, AV_PIX_FMT_AMF_SURFACE, AV_PIX_FMT_NV12},
#endif
    {CodecKind::H264, "h264_d3d12va", "h264_d3d12va", "d3d12-texture/d3d12va", true, false, AV_HWDEVICE_TYPE_D3D12VA, AV_PIX_FMT_D3D12, AV_PIX_FMT_NV12},
    {CodecKind::H264, "h264_vaapi", "h264_vaapi", "vaapi/dmabuf", true, false, AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI, AV_PIX_FMT_NV12},
#if !defined(__linux__)
    {CodecKind::H264, "h264_vulkan", "h264_vulkan", "vulkan", true, false, AV_HWDEVICE_TYPE_VULKAN, AV_PIX_FMT_VULKAN, AV_PIX_FMT_NV12},
#endif
    {CodecKind::H264, "h264_v4l2m2m", "h264_v4l2m2m", "v4l2-dmabuf", true, true, AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_NV12},
    {CodecKind::H264, "h264_rkmpp", "h264_rkmpp", "rkmpp", true, false, AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_NV12},
    {CodecKind::H264, "h264_cuvid", "h264_cuvid", "cuda", false, true, AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, AV_PIX_FMT_NV12},
    {CodecKind::H264, "h264_mf", "h264_mf", "d3d11-texture/mf", true, false, kNativeD3D11Device, kNativeD3D11Format, AV_PIX_FMT_NV12},
    {CodecKind::H264, "h264_sw", "h264", "", false, true, AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_YUV420P},
    {CodecKind::H264, "libopenh264", "libopenh264", "", true, true, AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_YUV420P},

#if defined(AIRAN_FFMPEG_NATIVE_HW_DECODER)
    {CodecKind::VP8, "vp8", "vp8", kNativeDecoderZeroCopy, false, true, kNativeDecoderDevice, kNativeDecoderFormat, AV_PIX_FMT_NV12},
#endif
    {CodecKind::VP8, "vp8_vaapi", "vp8_vaapi", "vaapi/dmabuf", true, false, AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI, AV_PIX_FMT_NV12},
    {CodecKind::VP8, "vp8_v4l2m2m", "vp8_v4l2m2m", "v4l2-dmabuf", true, true, AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_NV12},
    {CodecKind::VP8, "vp8_cuvid", "vp8_cuvid", "cuda", false, true, AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, AV_PIX_FMT_NV12},
    {CodecKind::VP8, "vp8_qsv", "vp8_qsv", "qsv/d3d11", false, true, AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, AV_PIX_FMT_NV12},
    {CodecKind::VP8, "vp8_sw", "vp8", "", false, true, AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_YUV420P},
    {CodecKind::VP8, "libvpx", "libvpx", "", true, true, AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_YUV420P},

#if defined(AIRAN_FFMPEG_NATIVE_HW_DECODER)
    {CodecKind::VP9, "vp9", "vp9", kNativeDecoderZeroCopy, false, true, kNativeDecoderDevice, kNativeDecoderFormat, AV_PIX_FMT_NV12},
#endif
    {CodecKind::VP9, "vp9_qsv", "vp9_qsv", "qsv/d3d11", true, true, AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, AV_PIX_FMT_NV12},
    {CodecKind::VP9, "vp9_vaapi", "vp9_vaapi", "vaapi/dmabuf", true, false, AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI, AV_PIX_FMT_NV12},
    {CodecKind::VP9, "vp9_cuvid", "vp9_cuvid", "cuda", false, true, AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, AV_PIX_FMT_NV12},
#if defined(AIRAN_FFMPEG_HAVE_AMF)
    {CodecKind::VP9, "vp9_amf", "vp9_amf", "amf-surface", false, true, AV_HWDEVICE_TYPE_AMF, AV_PIX_FMT_AMF_SURFACE, AV_PIX_FMT_NV12},
#endif
    {CodecKind::VP9, "vp9_sw", "vp9", "", false, true, AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_YUV420P},
    {CodecKind::VP9, "libvpx-vp9", "libvpx-vp9", "", true, true, AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_YUV420P},

    {CodecKind::AV1, "av1_nvenc", "av1_nvenc", "d3d11-texture/nvenc", true, false, kNativeD3D11Device, kNativeD3D11Format, AV_PIX_FMT_NV12},
    {CodecKind::AV1, "av1_qsv", "av1_qsv", "qsv/d3d11", true, true, AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, AV_PIX_FMT_NV12},
#if defined(AIRAN_FFMPEG_HAVE_AMF)
    {CodecKind::AV1, "av1_amf", "av1_amf", "amf-surface", true, true, AV_HWDEVICE_TYPE_AMF, AV_PIX_FMT_AMF_SURFACE, AV_PIX_FMT_NV12},
#endif
    {CodecKind::AV1, "av1_vaapi", "av1_vaapi", "vaapi/dmabuf", true, false, AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI, AV_PIX_FMT_NV12},
    {CodecKind::AV1, "av1_cuvid", "av1_cuvid", "cuda", false, true, AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, AV_PIX_FMT_NV12},
    {CodecKind::AV1, "av1_d3d11va", "av1", "d3d11-texture/d3d11va", false, true, kNativeD3D11Device, kNativeD3D11Format, AV_PIX_FMT_NV12},
};
#endif


const CodecProbe *codecProbes(size_t *count)
{
#if defined(AIRAN_HAVE_FFMPEG)
    if (count)
        *count = sizeof(kCodecProbes) / sizeof(kCodecProbes[0]);
    return kCodecProbes;
#else
    if (count)
        *count = 0;
    return nullptr;
#endif
}

} // namespace airan::media::ffmpeg
