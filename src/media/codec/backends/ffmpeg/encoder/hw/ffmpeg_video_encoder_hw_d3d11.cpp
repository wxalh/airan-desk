#include "media/codec/backends/ffmpeg/encoder/core/ffmpeg_video_encoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"
#include "common/logger_manager.h"

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include "rtc/core/native_d3d11_video_frame_buffer.h"

#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#endif

#include <api/video/video_frame.h>
#include <rtc_base/logging.h>

#include <cerrno>
#include <vector>

namespace airan::media::ffmpeg
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
namespace
{
template <typename T>

void releaseComVoid(void *&ptr)
{
    if (!ptr)
        return;
    static_cast<T *>(ptr)->Release();
    ptr = nullptr;
}

AVPixelFormat pixelFormatForD3D11Texture(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return AV_PIX_FMT_BGRA;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return AV_PIX_FMT_RGBA;
    case DXGI_FORMAT_NV12:
        return AV_PIX_FMT_NV12;
    default:
        return AV_PIX_FMT_NONE;
    }
}

DXGI_FORMAT normalizedRgbFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
        return format;
    }
}

bool isRgbD3D11TextureFormat(DXGI_FORMAT format)
{
    return pixelFormatForD3D11Texture(format) == AV_PIX_FMT_BGRA ||
           pixelFormatForD3D11Texture(format) == AV_PIX_FMT_RGBA;
}

void appendUniqueBindFlags(std::vector<UINT> &flags, UINT value)
{
    for (const UINT existing : flags)
    {
        if (existing == value)
            return;
    }
    flags.push_back(value);
}

std::vector<UINT> d3d11FrameBindFlagCandidates(bool qsv)
{
    std::vector<UINT> candidates;
    if (qsv)
    {
        appendUniqueBindFlags(candidates, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DECODER);
        appendUniqueBindFlags(candidates, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_VIDEO_ENCODER);
        appendUniqueBindFlags(candidates, D3D11_BIND_SHADER_RESOURCE);
        appendUniqueBindFlags(candidates, D3D11_BIND_DECODER);
    }
    else
    {
        appendUniqueBindFlags(candidates, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_VIDEO_ENCODER);
        appendUniqueBindFlags(candidates, D3D11_BIND_SHADER_RESOURCE);
        appendUniqueBindFlags(candidates, D3D11_BIND_VIDEO_ENCODER);
    }
    appendUniqueBindFlags(candidates, 0);
    return candidates;
}

} // namespace
#endif


void FfmpegVideoEncoder::releaseD3D11VideoProcessor()
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    releaseComVoid<ID3D11Texture2D>(m_d3d11Nv12Texture);
    releaseComVoid<ID3D11VideoProcessor>(m_d3d11VideoProcessor);
    releaseComVoid<ID3D11VideoProcessorEnumerator>(m_d3d11VideoEnumerator);
    releaseComVoid<ID3D11VideoContext>(m_d3d11VideoContext);
    releaseComVoid<ID3D11VideoDevice>(m_d3d11VideoDevice);
    m_d3d11InputWidth = 0;
    m_d3d11InputHeight = 0;
    m_d3d11OutputWidth = 0;
    m_d3d11OutputHeight = 0;
    m_d3d11IntermediateFormat = 0;
#endif
}


AVFrame *FfmpegVideoEncoder::createHardwareFrameFromNativeD3D11(const webrtc::VideoFrame &frame)
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (!m_probe ||
        (m_probe->hardwarePixelFormat != AV_PIX_FMT_D3D11 && m_probe->deviceType != AV_HWDEVICE_TYPE_QSV))
    {
        LOG_WARN("Airan FFmpeg native D3D11 skipped: current backend does not accept D3D11 frames");
        return nullptr;
    }
    const auto *native = rtc::asD3D11TextureFrameBuffer(frame.video_frame_buffer().get());
    if (!native || !native->texture() || !native->device())
    {
        LOG_WARN("Airan FFmpeg native D3D11 skipped: missing native texture buffer");
        return nullptr;
    }
    if (!m_ctx || m_ctx->width <= 0 || m_ctx->height <= 0)
    {
        LOG_WARN("Airan FFmpeg native D3D11 skipped: encoder context is not ready");
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> textureDevice;
    native->texture()->GetDevice(&textureDevice);
    if (!textureDevice)
    {
        LOG_WARN("Airan FFmpeg native D3D11 skipped: texture has no D3D11 device");
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediateContext =
        rtc::getD3D11ImmediateContext(textureDevice.Get());
    if (!immediateContext)
    {
        LOG_WARN("Airan FFmpeg native D3D11 skipped: missing immediate context");
        return nullptr;
    }

    rtc::enableD3D11MultithreadProtection(textureDevice.Get());

    Microsoft::WRL::ComPtr<ID3D11Texture2D> encodeTexture;
    DXGI_FORMAT encodeFormat = native->format();
    UINT encodeSubresource = native->subresource();
    const bool nativeIsRgb = isRgbD3D11TextureFormat(native->format());
    const bool encodeWithQsv = m_probe->deviceType == AV_HWDEVICE_TYPE_QSV;
    const bool canWrapNativeRgbForEncoder = false;
    if (canWrapNativeRgbForEncoder)
    {
        encodeTexture = native->texture();
        encodeFormat = normalizedRgbFormat(native->format());
        encodeSubresource = native->subresource();
    }
    else if (nativeIsRgb)
    {
        Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;
        Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext;
        if (FAILED(textureDevice.As(&videoDevice)) || FAILED(immediateContext.As(&videoContext)) ||
            !videoDevice || !videoContext)
        {
            LOG_WARN("Airan FFmpeg native D3D11 skipped: device does not expose video processor");
            return nullptr;
        }

        D3D11_TEXTURE2D_DESC srcDesc{};
        native->texture()->GetDesc(&srcDesc);
        const DXGI_FORMAT intermediateFormat = DXGI_FORMAT_NV12;
        const bool recreateVideoProcessor =
            !m_d3d11VideoDevice || !m_d3d11VideoContext || !m_d3d11VideoEnumerator ||
            !m_d3d11VideoProcessor || !m_d3d11Nv12Texture ||
            m_d3d11InputWidth != static_cast<int>(srcDesc.Width) ||
            m_d3d11InputHeight != static_cast<int>(srcDesc.Height) ||
            m_d3d11OutputWidth != m_ctx->width ||
            m_d3d11OutputHeight != m_ctx->height ||
            m_d3d11IntermediateFormat != static_cast<int>(intermediateFormat);

        if (recreateVideoProcessor)
        {
            releaseD3D11VideoProcessor();

            D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
            contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
            contentDesc.InputWidth = srcDesc.Width;
            contentDesc.InputHeight = srcDesc.Height;
            contentDesc.OutputWidth = static_cast<UINT>(m_ctx->width);
            contentDesc.OutputHeight = static_cast<UINT>(m_ctx->height);
            contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
            contentDesc.InputFrameRate = {static_cast<UINT>(m_ctx && m_ctx->framerate.num > 0 ? m_ctx->framerate.num : 30),
                                          static_cast<UINT>(m_ctx && m_ctx->framerate.den > 0 ? m_ctx->framerate.den : 1)};
            contentDesc.OutputFrameRate = contentDesc.InputFrameRate;

            ID3D11VideoProcessorEnumerator *enumerator = nullptr;
            if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&contentDesc, &enumerator)) || !enumerator)
            {
                LOG_WARN("Airan D3D11 video processor enumerator creation failed; input={}x{}, output={}x{}",
                         srcDesc.Width, srcDesc.Height,
                         m_ctx->width, m_ctx->height);
                return nullptr;
            }

            ID3D11VideoProcessor *processor = nullptr;
            if (FAILED(videoDevice->CreateVideoProcessor(enumerator, 0, &processor)) || !processor)
            {
                LOG_WARN("Airan D3D11 video processor creation failed");
                enumerator->Release();
                return nullptr;
            }

            D3D11_TEXTURE2D_DESC intermediateDesc{};
            intermediateDesc.Width = static_cast<UINT>(m_ctx->width);
            intermediateDesc.Height = static_cast<UINT>(m_ctx->height);
            intermediateDesc.MipLevels = 1;
            intermediateDesc.ArraySize = 1;
            intermediateDesc.Format = intermediateFormat;
            intermediateDesc.SampleDesc.Count = 1;
            intermediateDesc.Usage = D3D11_USAGE_DEFAULT;
            intermediateDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

            ID3D11Texture2D *intermediateTexture = nullptr;
            if (FAILED(textureDevice->CreateTexture2D(&intermediateDesc, nullptr, &intermediateTexture)) ||
                !intermediateTexture)
            {
                LOG_WARN("Airan D3D11 intermediate texture creation failed; format={}, size={}x{}",
                         static_cast<int>(intermediateDesc.Format),
                         intermediateDesc.Width, intermediateDesc.Height);
                processor->Release();
                enumerator->Release();
                return nullptr;
            }

            m_d3d11VideoDevice = videoDevice.Detach();
            m_d3d11VideoContext = videoContext.Detach();
            m_d3d11VideoEnumerator = enumerator;
            m_d3d11VideoProcessor = processor;
            m_d3d11Nv12Texture = intermediateTexture;
            m_d3d11InputWidth = static_cast<int>(srcDesc.Width);
            m_d3d11InputHeight = static_cast<int>(srcDesc.Height);
            m_d3d11OutputWidth = m_ctx->width;
            m_d3d11OutputHeight = m_ctx->height;
            m_d3d11IntermediateFormat = static_cast<int>(intermediateFormat);
        }

        auto *videoDeviceRaw = static_cast<ID3D11VideoDevice *>(m_d3d11VideoDevice);
        auto *videoContextRaw = static_cast<ID3D11VideoContext *>(m_d3d11VideoContext);
        auto *enumeratorRaw = static_cast<ID3D11VideoProcessorEnumerator *>(m_d3d11VideoEnumerator);
        auto *processorRaw = static_cast<ID3D11VideoProcessor *>(m_d3d11VideoProcessor);
        auto *nv12TextureRaw = static_cast<ID3D11Texture2D *>(m_d3d11Nv12Texture);
        if (!videoDeviceRaw || !videoContextRaw || !enumeratorRaw || !processorRaw || !nv12TextureRaw)
        {
            LOG_WARN("Airan FFmpeg native D3D11 skipped: cached video processor state is incomplete");
            return nullptr;
        }

        Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc{};
        inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        inputViewDesc.Texture2D.MipSlice = 0;
        inputViewDesc.Texture2D.ArraySlice = native->subresource();
        if (FAILED(videoDeviceRaw->CreateVideoProcessorInputView(native->texture(),
                                                                  enumeratorRaw,
                                                                  &inputViewDesc,
                                                                  &inputView)))
        {
            LOG_WARN("Airan D3D11 video processor input view creation failed; format={}, subresource={}",
                     static_cast<int>(srcDesc.Format),
                     native->subresource());
            return nullptr;
        }

        Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc{};
        outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        outputViewDesc.Texture2D.MipSlice = 0;
        if (FAILED(videoDeviceRaw->CreateVideoProcessorOutputView(nv12TextureRaw,
                                                                   enumeratorRaw,
                                                                   &outputViewDesc,
                                                                   &outputView)))
        {
            LOG_WARN("Airan D3D11 video processor output view creation failed");
            return nullptr;
        }

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView.Get();

        const RECT sourceRect{0, 0, static_cast<LONG>(srcDesc.Width), static_cast<LONG>(srcDesc.Height)};
        const RECT outputRect{0, 0, static_cast<LONG>(m_ctx->width), static_cast<LONG>(m_ctx->height)};
        videoContextRaw->VideoProcessorSetStreamSourceRect(processorRaw, 0, TRUE, &sourceRect);
        videoContextRaw->VideoProcessorSetStreamDestRect(processorRaw, 0, TRUE, &outputRect);
        videoContextRaw->VideoProcessorSetOutputTargetRect(processorRaw, TRUE, &outputRect);

        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColorSpace{};
        inputColorSpace.RGB_Range = 0;      // RGB input is full range.
        inputColorSpace.YCbCr_Matrix = 1;   // Keep the RGB->YUV conversion on BT.709.
        inputColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        videoContextRaw->VideoProcessorSetStreamColorSpace(processorRaw, 0, &inputColorSpace);

        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColorSpace{};
        outputColorSpace.RGB_Range = 0;
        outputColorSpace.YCbCr_Matrix = 1;  // BT.709 for HD desktop content.
        outputColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        videoContextRaw->VideoProcessorSetOutputColorSpace(processorRaw, &outputColorSpace);

        if (FAILED(videoContextRaw->VideoProcessorBlt(processorRaw, outputView.Get(), 0, 1, &stream)))
        {
            LOG_WARN("Airan D3D11 video processor blit failed");
            return nullptr;
        }

        encodeTexture = nv12TextureRaw;
        encodeFormat = intermediateFormat;
        encodeSubresource = 0;
    }
    else
    {
        if (native->width() != m_ctx->width || native->height() != m_ctx->height)
        {
            LOG_WARN("Airan FFmpeg native D3D11 skipped: non-RGB native frame size mismatch; frame={}x{}, encoder={}x{}",
                     native->width(), native->height(),
                     m_ctx->width, m_ctx->height);
            return nullptr;
        }
        encodeTexture = native->texture();
    }

    ID3D11Device *boundDevice = nullptr;
    if (m_nativeD3D11Device)
    {
        auto *deviceContext = reinterpret_cast<AVHWDeviceContext *>(m_nativeD3D11Device->data);
        auto *d3d11Context = deviceContext ? reinterpret_cast<AVD3D11VADeviceContext *>(deviceContext->hwctx) : nullptr;
        boundDevice = d3d11Context ? d3d11Context->device : nullptr;
    }

    if (!rtc::sameD3D11Device(boundDevice, textureDevice.Get()))
    {
        av_buffer_unref(&m_nativeD3D11Frames);
        av_buffer_unref(&m_nativeD3D11Device);
        releaseQsvHwMapGraph();
        m_nativeD3D11SessionBound = false;

        AVBufferRef *deviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!deviceRef)
        {
            LOG_WARN("Airan FFmpeg native D3D11 device context allocation failed");
            return nullptr;
        }

        auto *deviceContext = reinterpret_cast<AVHWDeviceContext *>(deviceRef->data);
        auto *d3d11Context = reinterpret_cast<AVD3D11VADeviceContext *>(deviceContext->hwctx);
        d3d11Context->device = textureDevice.Get();
        d3d11Context->device_context = immediateContext.Get();
        d3d11Context->BindFlags = 0;
        d3d11Context->MiscFlags = 0;
        d3d11Context->device->AddRef();
        d3d11Context->device_context->AddRef();

        const int deviceResult = av_hwdevice_ctx_init(deviceRef);
        if (deviceResult < 0)
        {
            LOG_WARN("Airan FFmpeg native D3D11 device context init failed; backend={}, error={}",
                     m_probe && m_probe->backend ? m_probe->backend : "",
                     ffmpegErrorText(deviceResult));
            av_buffer_unref(&deviceRef);
            return nullptr;
        }
        m_nativeD3D11Device = deviceRef;
    }

    D3D11_TEXTURE2D_DESC desc{};
    encodeTexture->GetDesc(&desc);
    const AVPixelFormat swFormat = pixelFormatForD3D11Texture(encodeFormat);
    if (swFormat == AV_PIX_FMT_NONE)
    {
        LOG_WARN("Airan FFmpeg native D3D11 unsupported texture format; backend={}, format={}",
                 m_probe && m_probe->backend ? m_probe->backend : "",
                 static_cast<int>(encodeFormat));
        return nullptr;
    }
    bool recreateFrames = m_nativeD3D11Frames == nullptr;
    if (m_nativeD3D11Frames)
    {
        auto *existingFrames = reinterpret_cast<AVHWFramesContext *>(m_nativeD3D11Frames->data);
        recreateFrames = !existingFrames ||
                         existingFrames->width != static_cast<int>(desc.Width) ||
                         existingFrames->height != static_cast<int>(desc.Height) ||
                         existingFrames->sw_format != swFormat;
    }
    if (recreateFrames)
    {
        av_buffer_unref(&m_nativeD3D11Frames);
        releaseQsvHwMapGraph();
        m_nativeD3D11SessionBound = false;

        int lastFramesResult = AVERROR(EINVAL);
        UINT lastBindFlags = 0;
        for (const UINT bindFlags : d3d11FrameBindFlagCandidates(encodeWithQsv))
        {
            AVBufferRef *framesRef = av_hwframe_ctx_alloc(m_nativeD3D11Device);
            if (!framesRef)
            {
                LOG_WARN("Airan FFmpeg native D3D11 frame context allocation failed");
                return nullptr;
            }
            auto *frames = reinterpret_cast<AVHWFramesContext *>(framesRef->data);
            frames->format = AV_PIX_FMT_D3D11;
            frames->sw_format = swFormat;
            frames->width = desc.Width;
            frames->height = desc.Height;
            frames->initial_pool_size = 8;
            auto *d3d11Frames = reinterpret_cast<AVD3D11VAFramesContext *>(frames->hwctx);
            if (d3d11Frames)
            {
                d3d11Frames->BindFlags = bindFlags;
                d3d11Frames->MiscFlags = 0;
            }
            const int framesResult = av_hwframe_ctx_init(framesRef);
            if (framesResult >= 0)
            {
                m_nativeD3D11Frames = framesRef;
                m_nativeD3D11SessionBound = false;
                LOG_DEBUG("Airan FFmpeg native D3D11 frame context ready; backend={}, source_format={}, sw_format={}, bind_flags={}, size={}x{}",
                          m_probe && m_probe->backend ? m_probe->backend : "",
                          static_cast<int>(encodeFormat),
                          static_cast<int>(swFormat),
                          bindFlags,
                          frames->width, frames->height);
                break;
            }

            lastFramesResult = framesResult;
            lastBindFlags = bindFlags;
            LOG_WARN("Airan FFmpeg native D3D11 frame context init attempt failed; backend={}, source_format={}, sw_format={}, bind_flags={}, size={}x{}, error={}",
                     m_probe && m_probe->backend ? m_probe->backend : "",
                     static_cast<int>(encodeFormat),
                     static_cast<int>(swFormat),
                     bindFlags,
                     frames->width, frames->height,
                     ffmpegErrorText(framesResult));
            av_buffer_unref(&framesRef);
        }
        if (!m_nativeD3D11Frames)
        {
            LOG_WARN("Airan FFmpeg native D3D11 frame context init failed after all bind flags; backend={}, source_format={}, sw_format={}, last_bind_flags={}, size={}x{}, error={}",
                     m_probe && m_probe->backend ? m_probe->backend : "",
                     static_cast<int>(encodeFormat),
                     static_cast<int>(swFormat),
                     lastBindFlags,
                     desc.Width, desc.Height,
                     ffmpegErrorText(lastFramesResult));
            return nullptr;
        }
    }

    AVFrame *hwFrame = av_frame_alloc();
    if (!hwFrame)
        return nullptr;
    const int bufferResult = av_hwframe_get_buffer(m_nativeD3D11Frames, hwFrame, 0);
    if (bufferResult < 0)
    {
        LOG_WARN("Airan FFmpeg native D3D11 frame buffer allocation failed; backend={}, sw_format={}, size={}x{}, error={}",
                 m_probe && m_probe->backend ? m_probe->backend : "",
                 static_cast<int>(swFormat),
                 desc.Width, desc.Height,
                 ffmpegErrorText(bufferResult));
        av_frame_free(&hwFrame);
        return nullptr;
    }
    hwFrame->color_range = AVCOL_RANGE_MPEG;
    hwFrame->colorspace = AVCOL_SPC_BT709;
    hwFrame->color_primaries = AVCOL_PRI_BT709;
    hwFrame->color_trc = AVCOL_TRC_BT709;
    hwFrame->chroma_location = AVCHROMA_LOC_LEFT;

    auto *dstTexture = reinterpret_cast<ID3D11Texture2D *>(hwFrame->data[0]);
    const UINT dstIndex = static_cast<UINT>(reinterpret_cast<uintptr_t>(hwFrame->data[1]));
    if (!dstTexture)
    {
        LOG_WARN("Airan FFmpeg native D3D11 frame buffer has no destination texture");
        av_frame_free(&hwFrame);
        return nullptr;
    }
    const UINT dstSubresource = D3D11CalcSubresource(0, dstIndex, 1);
    immediateContext->CopySubresourceRegion(dstTexture, dstSubresource,
                                            0, 0, 0,
                                            encodeTexture.Get(), encodeSubresource, nullptr);

    if (m_probe->deviceType == AV_HWDEVICE_TYPE_QSV)
    {
        AVFrame *qsvFrame = mapD3D11FrameToQsv(hwFrame);
        if (qsvFrame)
        {
            qsvFrame->color_range = AVCOL_RANGE_MPEG;
            qsvFrame->colorspace = AVCOL_SPC_BT709;
            qsvFrame->color_primaries = AVCOL_PRI_BT709;
            qsvFrame->color_trc = AVCOL_TRC_BT709;
            qsvFrame->chroma_location = AVCHROMA_LOC_LEFT;
        }
        av_frame_free(&hwFrame);
        return qsvFrame;
    }

    if (!m_nativeD3D11SessionBound && !rebindD3D11EncoderSession(m_nativeD3D11Device, m_nativeD3D11Frames))
    {
        LOG_WARN("Airan FFmpeg native D3D11 encoder session rebind failed; backend={}, sw_format={}, size={}x{}",
                 m_probe && m_probe->backend ? m_probe->backend : "",
                 static_cast<int>(swFormat),
                 desc.Width, desc.Height);
        av_frame_free(&hwFrame);
        return nullptr;
    }
    return hwFrame;
#else
    (void)frame;
    return nullptr;
#endif
}

} // namespace airan::media::ffmpeg

#endif
