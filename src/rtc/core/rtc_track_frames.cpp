#include "rtc/core/rtc_internal.h"
#include "rtc/core/native_d3d11_video_frame_buffer.h"

#include "common/logger_manager.h"

#include <QtGlobal>

#include <cstring>
#include <utility>

namespace rtc
{


void Track::captureAudioFrame(const void *audioData, int bitsPerSample, int sampleRate, size_t channels, size_t frames)
{
    Q_UNUSED(audioData);
    Q_UNUSED(bitsPerSample);
    Q_UNUSED(sampleRate);
    Q_UNUSED(channels);
    Q_UNUSED(frames);
    bool expected = false;
    if (m_customAudioWarningLogged.compare_exchange_strong(expected, true))
    {
        LOG_WARN("Custom system-loopback audio injection is not wired to Google WebRTC native ADM yet; microphone/remote audio still use WebRTC audio device");
    }
}


void Track::OnFrame(const webrtc::VideoFrame &frame)
{
    if (!m_open.load())
        return;
    const int width = frame.width();
    const int height = frame.height();
    constexpr size_t kMaxBgraFrameBytes = 256ULL * 1024 * 1024;
    if (width <= 0 || height <= 0 ||
        static_cast<size_t>(width) > kMaxBgraFrameBytes / 4 / static_cast<size_t>(height))
    {
        LOG_WARN("Rejected invalid or oversized video frame: mid={}, size={}x{}", m_mid, width, height);
        return;
    }
    bool expected = false;
    if (m_firstFrameLogged.compare_exchange_strong(expected, true))
        LOG_DEBUG("First Google WebRTC video frame received by wrapper: mid={}, size={}x{}", m_mid, width, height);

    auto buffer = frame.video_frame_buffer();
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    D3D11FrameCallback d3d11Callback;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (!m_open.load())
            return;
        d3d11Callback = m_onD3D11Frame;
    }
    if (d3d11Callback && buffer)
    {
        if (const auto *native = asD3D11TextureFrameBuffer(buffer.get()))
        {
            D3D11VideoFrame d3dFrame;
            d3dFrame.device = native->device();
            d3dFrame.texture = native->texture();
            d3dFrame.subresource = native->subresource();
            d3dFrame.width = native->width();
            d3dFrame.height = native->height();
            d3dFrame.format = native->format();
            d3dFrame.timestampUs = frame.timestamp_us();
            d3d11Callback(std::move(d3dFrame), FrameInfo(std::chrono::microseconds(frame.timestamp_us())));
            return;
        }
    }
#endif

    FrameCallback frameCallback;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (!m_open.load())
            return;
        frameCallback = m_onFrame;
    }
    if (!frameCallback)
        return;

    const size_t pixelBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    binary out(sizeof(width) + sizeof(height) + pixelBytes);
    std::memcpy(out.data(), &width, sizeof(width));
    std::memcpy(out.data() + sizeof(width), &height, sizeof(height));
    uint8_t *pixels = reinterpret_cast<uint8_t *>(out.data() + sizeof(width) + sizeof(height));
    if (!convertFrameToQtBgra(frame, pixels, width, height))
    {
        bool failureExpected = false;
        if (m_frameConvertFailureLogged.compare_exchange_strong(failureExpected, true))
            LOG_WARN("Failed to convert Google WebRTC video frame to Qt BGRA: mid={}, size={}x{}", m_mid, width, height);
        return;
    }
    frameCallback(std::move(out), FrameInfo(std::chrono::microseconds(frame.timestamp_us())));
}


void Track::OnData(const void *audio_data,
                   int bits_per_sample,
                   int sample_rate,
                   size_t number_of_channels,
                   size_t number_of_frames)
{
    if (!m_open.load() || !audio_data || bits_per_sample <= 0 || sample_rate <= 0 || number_of_channels == 0 || number_of_frames == 0)
        return;

    if (bits_per_sample % 8 != 0)
        return;
    const size_t bytesPerSample = static_cast<size_t>(bits_per_sample / 8);
    constexpr size_t kMaxAudioFrameBytes = 16 * 1024 * 1024;
    if (bytesPerSample == 0 ||
        number_of_channels > kMaxAudioFrameBytes / bytesPerSample ||
        number_of_frames > kMaxAudioFrameBytes / (number_of_channels * bytesPerSample))
    {
        LOG_WARN("Rejected invalid or oversized audio frame: frames={}, channels={}, bitsPerSample={}",
                 number_of_frames, number_of_channels, bits_per_sample);
        return;
    }
    const size_t bytes = number_of_frames * number_of_channels * bytesPerSample;
    auto out = bytesToBinary(static_cast<const uint8_t *>(audio_data), bytes);
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_open.load() && m_onFrame)
        m_onFrame(std::move(out), FrameInfo{});
}

} // namespace rtc
