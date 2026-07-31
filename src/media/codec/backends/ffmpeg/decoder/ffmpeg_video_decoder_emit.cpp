#include "media/codec/backends/ffmpeg/decoder/ffmpeg_video_decoder.h"
#include "rtc/core/native_d3d11_video_frame_buffer.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>
#include "common/logger_manager.h"

#if defined(_WIN32)
#include <wrl/client.h>
#endif

namespace airan::media::ffmpeg
{
namespace
{
#if AIRAN_WEBRTC_MILESTONE >= 144
uint32_t encodedRtpTimestamp(const webrtc::EncodedImage &image)
{
    return image.RtpTimestamp();
}

webrtc::VideoFrame::Builder &setBuilderRtpTimestamp(webrtc::VideoFrame::Builder &builder, uint32_t timestamp)
{
    return builder.set_rtp_timestamp(timestamp);
}
#else
uint32_t encodedRtpTimestamp(const webrtc::EncodedImage &image)
{
    return image.Timestamp();
}

webrtc::VideoFrame::Builder &setBuilderRtpTimestamp(webrtc::VideoFrame::Builder &builder, uint32_t timestamp)
{
    return builder.set_timestamp_rtp(timestamp);
}
#endif
} // namespace


bool FfmpegVideoDecoder::emitFrame(AVFrame *frame, const webrtc::EncodedImage &inputImage, int64_t renderTimeMs)
{
    if (!frame || frame->width <= 0 || frame->height <= 0)
        return false;

    auto i420 = webrtc::I420Buffer::Create(frame->width, frame->height);
    if (!i420)
        return false;

    uint8_t *dstData[3] = {i420->MutableDataY(), i420->MutableDataU(), i420->MutableDataV()};
    int dstLinesize[3] = {i420->StrideY(), i420->StrideU(), i420->StrideV()};
    m_sws = sws_getCachedContext(m_sws,
                                 frame->width,
                                 frame->height,
                                 static_cast<AVPixelFormat>(frame->format),
                                 frame->width,
                                 frame->height,
                                 AV_PIX_FMT_YUV420P,
                                 SWS_FAST_BILINEAR,
                                 nullptr,
                                 nullptr,
                                 nullptr);
    if (!m_sws)
        return false;
    if (sws_scale(m_sws, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize) <= 0)
        return false;

    webrtc::VideoFrame::Builder builder;
    builder.set_video_frame_buffer(i420)
        .set_ntp_time_ms(inputImage.NtpTimeMs())
        .set_timestamp_ms(renderTimeMs);
    webrtc::VideoFrame decoded = setBuilderRtpTimestamp(builder, encodedRtpTimestamp(inputImage)).build();
    m_callback->Decoded(decoded);
    return true;
}


bool FfmpegVideoDecoder::emitD3D11Frame(AVFrame *frame, const webrtc::EncodedImage &inputImage, int64_t renderTimeMs)
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (!frame || !m_hwDevice || !m_callback || frame->format != AV_PIX_FMT_D3D11 ||
        frame->width <= 0 || frame->height <= 0)
    {
        LOG_WARN("FFmpeg D3D11 decoder output invalid: frame={}, hwDevice={}, callback={}, format={}, size={}x{}",
                 !!frame, !!m_hwDevice, !!m_callback,
                 (frame ? frame->format : AV_PIX_FMT_NONE),
                 (frame ? frame->width : 0), (frame ? frame->height : 0));
        return false;
    }

    auto *srcTexture = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
    const uint32_t srcSubresource = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame->data[1]));
    auto *deviceContext = reinterpret_cast<AVHWDeviceContext *>(m_hwDevice->data);
    auto *d3d11Context = deviceContext ? reinterpret_cast<AVD3D11VADeviceContext *>(deviceContext->hwctx) : nullptr;
    if (!srcTexture || !d3d11Context || !d3d11Context->device)
    {
        LOG_WARN("FFmpeg D3D11 decoder output missing texture/device: texture={}, d3d11Context={}, device={}",
                 !!srcTexture, !!d3d11Context, !!(d3d11Context ? d3d11Context->device : nullptr));
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device = d3d11Context->device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    if (!context)
    {
        LOG_WARN("FFmpeg D3D11 decoder output has no immediate context");
        return false;
    }

    D3D11_TEXTURE2D_DESC srcDesc = {};
    srcTexture->GetDesc(&srcDesc);
    D3D11_TEXTURE2D_DESC copyDesc = srcDesc;
    copyDesc.Width = static_cast<UINT>(frame->width);
    copyDesc.Height = static_cast<UINT>(frame->height);
    copyDesc.MipLevels = 1;
    copyDesc.ArraySize = 1;
    copyDesc.SampleDesc.Count = 1;
    copyDesc.SampleDesc.Quality = 0;
    copyDesc.Usage = D3D11_USAGE_DEFAULT;
    copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copyDesc.CPUAccessFlags = 0;
    copyDesc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> copyTexture;
    const HRESULT createResult = device->CreateTexture2D(&copyDesc, nullptr, &copyTexture);
    if (FAILED(createResult) || !copyTexture)
    {
        LOG_WARN("FFmpeg D3D11 decoder output copy texture creation failed: hr={}, srcFormat={}, bindFlags={}, miscFlags={}, size={}x{}",
                 static_cast<long>(createResult), static_cast<int>(srcDesc.Format),
                 srcDesc.BindFlags, srcDesc.MiscFlags, frame->width, frame->height);
        return false;
    }

    const D3D11_BOX visibleBox{
        0,
        0,
        0,
        static_cast<UINT>(frame->width),
        static_cast<UINT>(frame->height),
        1};
    context->CopySubresourceRegion(copyTexture.Get(), 0, 0, 0, 0, srcTexture, srcSubresource, &visibleBox);

    auto buffer = rtc::makeD3D11TextureFrameBuffer(device,
                                                   copyTexture,
                                                   0,
                                                   frame->width,
                                                   frame->height,
                                                   copyDesc.Format);
    if (!buffer)
    {
        LOG_WARN("FFmpeg D3D11 decoder output wrapper creation failed: format={}, size={}x{}",
                 static_cast<int>(copyDesc.Format), frame->width, frame->height);
        return false;
    }

    webrtc::VideoFrame::Builder builder;
    builder.set_video_frame_buffer(buffer)
        .set_ntp_time_ms(inputImage.NtpTimeMs())
        .set_timestamp_ms(renderTimeMs);
    webrtc::VideoFrame decoded = setBuilderRtpTimestamp(builder, encodedRtpTimestamp(inputImage)).build();
    m_callback->Decoded(decoded);
    return true;
#else
    (void)frame;
    (void)inputImage;
    (void)renderTimeMs;
    return false;
#endif
}

} // namespace airan::media::ffmpeg

#endif
