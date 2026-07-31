#include "webrtc/ctl/webrtc_ctl.h"
#include <QByteArray>
#include <atomic>
#include <cstring>


void WebRtcCtl::onVideoFrameReceived(rtc::binary data, rtc::FrameInfo info)
{
    if (m_shutdownDone)
        return;
    noteSessionInboundActivity();
    noteSessionTransportProgress();
    LOG_TRACE("Video frame received: {} bytes, timestamp: {}", data.size(), info.timestamp);
    processVideoFrame(data, info);
}

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)


void WebRtcCtl::onD3D11VideoFrameReceived(rtc::D3D11VideoFrame frame)
{
    if (m_shutdownDone)
        return;
    noteSessionInboundActivity();
    noteSessionTransportProgress();
    static std::atomic_bool firstD3D11FrameLogged{false};
    bool expected = false;
    if (firstD3D11FrameLogged.compare_exchange_strong(expected, true))
    {
        LOG_DEBUG("First D3D11 video frame received by control side: {}x{}, format={}, timestamp={}",
                  frame.width,
                  frame.height,
                  static_cast<int>(frame.format),
                  frame.timestampUs);
    }
    else
    {
        LOG_TRACE("D3D11 video frame received: {}x{}, format={}, timestamp={}",
                  frame.width,
                  frame.height,
                  static_cast<int>(frame.format),
                  frame.timestampUs);
    }
    processD3D11VideoFrame(std::move(frame));
}
#endif


void WebRtcCtl::onVideoFrameBytesReceived(const QByteArray &data, qint64 timestampUs)
{
    if (m_shutdownDone)
        return;

    rtc::binary frame(static_cast<size_t>(data.size()));
    if (!data.isEmpty())
        std::memcpy(frame.data(), data.constData(), static_cast<size_t>(data.size()));

    rtc::FrameInfo info{std::chrono::microseconds(timestampUs)};
    onVideoFrameReceived(std::move(frame), info);
}

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)


void WebRtcCtl::onD3D11VideoFrameQueued(const rtc::D3D11VideoFrame &frame)
{
    if (m_shutdownDone)
        return;
    onD3D11VideoFrameReceived(frame);
}


void WebRtcCtl::disableD3D11VideoRendering()
{
    if (m_shutdownDone || !m_videoTrack)
        return;
    m_videoTrack->onD3D11Frame(nullptr);
    LOG_WARN("D3D11 video rendering disabled after renderer failure; native frames will use CPU fallback conversion");
}
#endif


void WebRtcCtl::onAudioFrameReceived(rtc::binary data, rtc::FrameInfo info)
{
    noteSessionInboundActivity();
    noteSessionTransportProgress();
    Q_UNUSED(data);
    Q_UNUSED(info);
    LOG_TRACE("Remote audio frame callback received; playback is handled by the libwebrtc audio device");
}
