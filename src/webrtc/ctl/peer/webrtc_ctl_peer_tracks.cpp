#include "webrtc/ctl/webrtc_ctl.h"
#include <QByteArray>
#include <QPointer>
#include <QMutexLocker>
#include <atomic>
#include <cstring>
#include <utility>

namespace
{
constexpr int kPendingCpuVideoFrame = 1;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
constexpr int kPendingD3D11VideoFrame = 2;
#endif
}


void WebRtcCtl::enqueueVideoFrameBytes(QByteArray data, qint64 timestampUs)
{
    if (data.isEmpty() || m_shutdownStarted.load())
        return;

    bool scheduleDrain = false;
    {
        QMutexLocker locker(&m_videoFrameIngressMutex);
        if (m_shutdownStarted.load())
            return;
        m_pendingVideoFrameBytes = std::move(data);
        m_pendingVideoFrameTimestampUs = timestampUs;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        m_pendingD3D11VideoFrame = rtc::D3D11VideoFrame{};
#endif
        m_pendingVideoFrameKind = kPendingCpuVideoFrame;
        if (!m_videoFrameIngressScheduled)
        {
            m_videoFrameIngressScheduled = true;
            scheduleDrain = true;
        }
    }

    if (scheduleDrain && m_callbackDispatcher)
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->drainVideoFrameIngress();
        });
    }
}


#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
void WebRtcCtl::enqueueD3D11VideoFrame(const rtc::D3D11VideoFrame &frame)
{
    if (!frame.isValid() || m_shutdownStarted.load())
        return;

    bool scheduleDrain = false;
    {
        QMutexLocker locker(&m_videoFrameIngressMutex);
        if (m_shutdownStarted.load())
            return;
        m_pendingVideoFrameBytes.clear();
        m_pendingVideoFrameTimestampUs = 0;
        m_pendingD3D11VideoFrame = frame;
        m_pendingVideoFrameKind = kPendingD3D11VideoFrame;
        if (!m_videoFrameIngressScheduled)
        {
            m_videoFrameIngressScheduled = true;
            scheduleDrain = true;
        }
    }

    if (scheduleDrain && m_callbackDispatcher)
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->drainVideoFrameIngress();
        });
    }
}
#endif


void WebRtcCtl::drainVideoFrameIngress()
{
    QByteArray cpuFrame;
    qint64 timestampUs = 0;
    int frameKind = 0;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    rtc::D3D11VideoFrame d3d11Frame;
#endif
    {
        QMutexLocker locker(&m_videoFrameIngressMutex);
        frameKind = m_pendingVideoFrameKind;
        m_pendingVideoFrameKind = 0;
        m_videoFrameIngressScheduled = false;
        if (frameKind == kPendingCpuVideoFrame)
        {
            cpuFrame.swap(m_pendingVideoFrameBytes);
            timestampUs = m_pendingVideoFrameTimestampUs;
        }
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        else if (frameKind == kPendingD3D11VideoFrame)
        {
            d3d11Frame = std::move(m_pendingD3D11VideoFrame);
            m_pendingD3D11VideoFrame = rtc::D3D11VideoFrame{};
        }
#endif
    }

    if (m_shutdownStarted.load())
        return;
    if (frameKind == kPendingCpuVideoFrame)
        onVideoFrameBytesReceived(cpuFrame, timestampUs);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    else if (frameKind == kPendingD3D11VideoFrame)
        onD3D11VideoFrameReceived(std::move(d3d11Frame));
#endif
}


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
    enqueueD3D11VideoFrame(frame);
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
