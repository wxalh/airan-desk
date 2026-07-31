#pragma once

#include "media/capture/core/airan_capture_frame.h"
#include "rtc/core/rtc.hpp"

#include <api/video/i420_buffer.h>
#include <api/video/video_frame_buffer.h>
#include <libyuv/convert.h>
#include <rtc_base/ref_counted_object.h>

#include <cstdint>
#include <string>
#include <utility>

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include <d3d10_1.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#endif

namespace rtc
{

constexpr const char *kAiranD3D11TextureStorage = "airan-d3d11-texture";

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
inline bool enableD3D11MultithreadProtection(ID3D11Device *device);
inline scoped_refptr<webrtc::VideoFrameBuffer> makeD3D11TextureFrameBuffer(
    Microsoft::WRL::ComPtr<ID3D11Device> device,
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,
    uint32_t subresource,
    int width,
    int height,
    DXGI_FORMAT format,
    std::shared_ptr<airan::media::FrameRelease> release);

class AiranD3D11TextureFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
    AiranD3D11TextureFrameBuffer(Microsoft::WRL::ComPtr<ID3D11Device> device,
                                 Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,
                                 uint32_t subresource,
                                 int width,
                                 int height,
                                 DXGI_FORMAT format,
                                 std::shared_ptr<airan::media::FrameRelease> release = {})
        : m_device(std::move(device)),
          m_texture(std::move(texture)),
          m_subresource(subresource),
          m_width(width),
          m_height(height),
          m_format(format),
          m_release(std::move(release))
    {
    }

    Type type() const override { return Type::kNative; }
    int width() const override { return m_width; }
    int height() const override { return m_height; }

#if AIRAN_WEBRTC_MILESTONE >= 144
    webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override { return readbackToI420(); }
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> CropAndScale(int offset_x,
                                                                 int offset_y,
                                                                 int crop_width,
                                                                 int crop_height,
                                                                 int scaled_width,
                                                                 int scaled_height) override
#else
    ::rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override { return readbackToI420(); }
    ::rtc::scoped_refptr<webrtc::VideoFrameBuffer> CropAndScale(int offset_x,
                                                               int offset_y,
                                                               int crop_width,
                                                               int crop_height,
                                                               int scaled_width,
                                                               int scaled_height) override
#endif
    {
        if (offset_x == 0 && offset_y == 0 &&
            crop_width == m_width && crop_height == m_height &&
            scaled_width == m_width && scaled_height == m_height)
        {
#if AIRAN_WEBRTC_MILESTONE >= 144
            return webrtc::scoped_refptr<webrtc::VideoFrameBuffer>(this);
#else
            return ::rtc::scoped_refptr<webrtc::VideoFrameBuffer>(this);
#endif
        }

        auto gpuScaled = gpuCropAndScale(offset_x,
                                         offset_y,
                                         crop_width,
                                         crop_height,
                                         scaled_width,
                                         scaled_height);
        if (gpuScaled)
            return gpuScaled;

        auto i420 = readbackToI420();
        if (!i420)
            return nullptr;
        auto output = webrtc::I420Buffer::Create(scaled_width, scaled_height);
        if (!output)
            return nullptr;
        output->CropAndScaleFrom(*i420, offset_x, offset_y, crop_width, crop_height);
        return output;
    }

#if AIRAN_WEBRTC_MILESTONE >= 144
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> GetMappedFrameBuffer(webrtc::ArrayView<Type> types) override
#else
    ::rtc::scoped_refptr<webrtc::VideoFrameBuffer> GetMappedFrameBuffer(::rtc::ArrayView<Type> types) override
#endif
    {
        for (const auto type : types)
        {
            if (type == Type::kI420)
                return readbackToI420();
        }
        return nullptr;
    }

#if AIRAN_WEBRTC_MILESTONE >= 144
    std::string storage_representation() const override { return kAiranD3D11TextureStorage; }
#endif

    ID3D11Device *device() const { return m_device.Get(); }
    ID3D11Texture2D *texture() const { return m_texture.Get(); }
    uint32_t subresource() const { return m_subresource; }
    DXGI_FORMAT format() const { return m_format; }

    ~AiranD3D11TextureFrameBuffer() override = default;

private:
#if AIRAN_WEBRTC_MILESTONE >= 144
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> gpuCropAndScale(int offsetX,
                                                                    int offsetY,
                                                                    int cropWidth,
                                                                    int cropHeight,
                                                                    int scaledWidth,
                                                                    int scaledHeight)
#else
    ::rtc::scoped_refptr<webrtc::VideoFrameBuffer> gpuCropAndScale(int offsetX,
                                                                  int offsetY,
                                                                  int cropWidth,
                                                                  int cropHeight,
                                                                  int scaledWidth,
                                                                  int scaledHeight)
#endif
    {
        if (!m_device || !m_texture || scaledWidth <= 0 || scaledHeight <= 0 ||
            cropWidth <= 0 || cropHeight <= 0 || offsetX < 0 || offsetY < 0 ||
            offsetX + cropWidth > m_width || offsetY + cropHeight > m_height)
        {
            return nullptr;
        }

        const bool supportedRgb =
            m_format == DXGI_FORMAT_B8G8R8A8_UNORM ||
            m_format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            m_format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            m_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if (!supportedRgb)
            return nullptr;

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        m_device->GetImmediateContext(&context);
        if (!context)
            return nullptr;

        enableD3D11MultithreadProtection(m_device.Get());

        Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;
        Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext;
        if (FAILED(m_device.As(&videoDevice)) || FAILED(context.As(&videoContext)) ||
            !videoDevice || !videoContext)
        {
            return nullptr;
        }

        D3D11_TEXTURE2D_DESC srcDesc = {};
        m_texture->GetDesc(&srcDesc);
        if (m_subresource >= srcDesc.ArraySize ||
            static_cast<UINT>(m_width) > srcDesc.Width ||
            static_cast<UINT>(m_height) > srcDesc.Height)
        {
            return nullptr;
        }
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
        contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        contentDesc.InputWidth = srcDesc.Width;
        contentDesc.InputHeight = srcDesc.Height;
        contentDesc.OutputWidth = static_cast<UINT>(scaledWidth);
        contentDesc.OutputHeight = static_cast<UINT>(scaledHeight);
        contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        contentDesc.InputFrameRate = {30, 1};
        contentDesc.OutputFrameRate = {30, 1};

        Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
        if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&contentDesc, &enumerator)) || !enumerator)
            return nullptr;

        Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor;
        if (FAILED(videoDevice->CreateVideoProcessor(enumerator.Get(), 0, &processor)) || !processor)
            return nullptr;

        DXGI_FORMAT outputFormat = m_format;
        if (outputFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            outputFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        else if (outputFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D11_TEXTURE2D_DESC outDesc = {};
        outDesc.Width = static_cast<UINT>(scaledWidth);
        outDesc.Height = static_cast<UINT>(scaledHeight);
        outDesc.MipLevels = 1;
        outDesc.ArraySize = 1;
        outDesc.Format = outputFormat;
        outDesc.SampleDesc.Count = 1;
        outDesc.Usage = D3D11_USAGE_DEFAULT;
        outDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> outputTexture;
        if (FAILED(m_device->CreateTexture2D(&outDesc, nullptr, &outputTexture)) || !outputTexture)
            return nullptr;

        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
        inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        inputViewDesc.Texture2D.MipSlice = 0;
        inputViewDesc.Texture2D.ArraySlice = m_subresource;

        Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
        if (FAILED(videoDevice->CreateVideoProcessorInputView(m_texture.Get(),
                                                              enumerator.Get(),
                                                              &inputViewDesc,
                                                              &inputView)) ||
            !inputView)
        {
            return nullptr;
        }

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = {};
        outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        outputViewDesc.Texture2D.MipSlice = 0;

        Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
        if (FAILED(videoDevice->CreateVideoProcessorOutputView(outputTexture.Get(),
                                                               enumerator.Get(),
                                                               &outputViewDesc,
                                                               &outputView)) ||
            !outputView)
        {
            return nullptr;
        }

        const RECT sourceRect{static_cast<LONG>(offsetX),
                              static_cast<LONG>(offsetY),
                              static_cast<LONG>(offsetX + cropWidth),
                              static_cast<LONG>(offsetY + cropHeight)};
        const RECT destRect{0, 0, static_cast<LONG>(scaledWidth), static_cast<LONG>(scaledHeight)};
        videoContext->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, &sourceRect);
        videoContext->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &destRect);
        videoContext->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE, &destRect);

        D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace = {};
        colorSpace.RGB_Range = 0;
        colorSpace.YCbCr_Matrix = 1;
        colorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        videoContext->VideoProcessorSetStreamColorSpace(processor.Get(), 0, &colorSpace);
        videoContext->VideoProcessorSetOutputColorSpace(processor.Get(), &colorSpace);

        D3D11_VIDEO_PROCESSOR_STREAM stream = {};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView.Get();
        if (FAILED(videoContext->VideoProcessorBlt(processor.Get(), outputView.Get(), 0, 1, &stream)))
            return nullptr;

        return makeD3D11TextureFrameBuffer(m_device,
                                           outputTexture,
                                           0,
                                           scaledWidth,
                                           scaledHeight,
                                           outputFormat,
                                           m_release);
    }

#if AIRAN_WEBRTC_MILESTONE >= 144
    webrtc::scoped_refptr<webrtc::I420Buffer> readbackToI420()
#else
    ::rtc::scoped_refptr<webrtc::I420Buffer> readbackToI420()
#endif
    {
        if (!m_device || !m_texture || m_width <= 0 || m_height <= 0)
            return nullptr;
        if (m_format != DXGI_FORMAT_B8G8R8A8_UNORM && m_format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            return nullptr;

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        m_device->GetImmediateContext(&context);
        if (!context)
            return nullptr;

        D3D11_TEXTURE2D_DESC srcDesc = {};
        m_texture->GetDesc(&srcDesc);
        if (m_subresource >= srcDesc.ArraySize ||
            static_cast<UINT>(m_width) > srcDesc.Width ||
            static_cast<UINT>(m_height) > srcDesc.Height)
        {
            return nullptr;
        }
        D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        stagingDesc.ArraySize = 1;
        stagingDesc.MipLevels = 1;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.SampleDesc.Quality = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(m_device->CreateTexture2D(&stagingDesc, nullptr, &staging)))
            return nullptr;
        context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, m_texture.Get(), m_subresource, nullptr);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return nullptr;

        auto buffer = webrtc::I420Buffer::Create(m_width, m_height);
        if (!buffer)
        {
            context->Unmap(staging.Get(), 0);
            return nullptr;
        }

        const auto *src = static_cast<const uint8_t *>(mapped.pData);
        if (!src || mapped.RowPitch < static_cast<UINT>(m_width * 4))
        {
            context->Unmap(staging.Get(), 0);
            return nullptr;
        }
        const int result = libyuv::ARGBToI420(src,
                                              static_cast<int>(mapped.RowPitch),
                                              buffer->MutableDataY(),
                                              buffer->StrideY(),
                                              buffer->MutableDataU(),
                                              buffer->StrideU(),
                                              buffer->MutableDataV(),
                                              buffer->StrideV(),
                                              m_width,
                                              m_height);
        context->Unmap(staging.Get(), 0);
        if (result != 0)
        {
            return nullptr;
        }
        return buffer;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
    uint32_t m_subresource = 0;
    int m_width = 0;
    int m_height = 0;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    std::shared_ptr<airan::media::FrameRelease> m_release;
};

inline scoped_refptr<webrtc::VideoFrameBuffer> makeD3D11TextureFrameBuffer(
    Microsoft::WRL::ComPtr<ID3D11Device> device,
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,
    uint32_t subresource,
    int width,
    int height,
    DXGI_FORMAT format,
    std::shared_ptr<airan::media::FrameRelease> release = {})
{
#if AIRAN_WEBRTC_MILESTONE >= 144
    return webrtc::scoped_refptr<webrtc::VideoFrameBuffer>(
        new webrtc::RefCountedObject<AiranD3D11TextureFrameBuffer>(std::move(device),
                                                                   std::move(texture),
                                                                   subresource,
                                                                   width,
                                                                   height,
                                                                   format,
                                                                   std::move(release)));
#else
    return ::rtc::scoped_refptr<webrtc::VideoFrameBuffer>(
        new ::rtc::RefCountedObject<AiranD3D11TextureFrameBuffer>(std::move(device),
                                                                  std::move(texture),
                                                                  subresource,
                                                                  width,
                                                                  height,
                                                                  format,
                                                                  std::move(release)));
#endif
}

inline const AiranD3D11TextureFrameBuffer *asD3D11TextureFrameBuffer(const webrtc::VideoFrameBuffer *buffer)
{
    if (!buffer || buffer->type() != webrtc::VideoFrameBuffer::Type::kNative)
    {
        return nullptr;
    }
#if AIRAN_WEBRTC_MILESTONE >= 144
    if (buffer->storage_representation() != kAiranD3D11TextureStorage)
        return nullptr;
#endif
    return static_cast<const AiranD3D11TextureFrameBuffer *>(buffer);
}

inline Microsoft::WRL::ComPtr<ID3D11DeviceContext> getD3D11ImmediateContext(ID3D11Device *device)
{
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if (device)
        device->GetImmediateContext(&context);
    return context;
}

inline bool enableD3D11MultithreadProtection(ID3D11Device *device)
{
    if (!device)
        return false;

    Microsoft::WRL::ComPtr<ID3D10Multithread> multithread;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&multithread))) || !multithread)
        return false;

    return multithread->SetMultithreadProtected(TRUE) != FALSE;
}

inline bool sameD3D11Device(ID3D11Device *lhs, ID3D11Device *rhs)
{
    return lhs && rhs && lhs == rhs;
}
#endif

} // namespace rtc
