#include "ui/control/control_window.h"

#include <QLabel>
#include <QMetaObject>
#include <QMutexLocker>

#include <utility>

namespace
{
constexpr int kPendingCpuFrame = 1;
constexpr int kPendingD3D11Frame = 2;
}


void ControlWindow::enqueueVideoFrame(const QImage &frame)
{
    if (frame.isNull() || isClosing())
        return;

    bool scheduleDrain = false;
    {
        QMutexLocker locker(&m_videoFrameMutex);
        if (m_closing.load())
            return;
        m_pendingVideoFrame = frame;
        m_pendingVideoFrameKind = kPendingCpuFrame;
        if (!m_videoFrameDrainScheduled)
        {
            m_videoFrameDrainScheduled = true;
            scheduleDrain = true;
        }
    }
    if (scheduleDrain)
        QMetaObject::invokeMethod(this, "drainPendingVideoFrame", Qt::QueuedConnection);
}


#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
void ControlWindow::enqueueD3D11VideoFrame(const rtc::D3D11VideoFrame &frame)
{
    if (!frame.isValid() || isClosing())
        return;

    bool scheduleDrain = false;
    {
        QMutexLocker locker(&m_videoFrameMutex);
        if (m_closing.load())
            return;
        m_pendingD3D11VideoFrame = frame;
        m_pendingVideoFrameKind = kPendingD3D11Frame;
        if (!m_videoFrameDrainScheduled)
        {
            m_videoFrameDrainScheduled = true;
            scheduleDrain = true;
        }
    }
    if (scheduleDrain)
        QMetaObject::invokeMethod(this, "drainPendingVideoFrame", Qt::QueuedConnection);
}
#endif


void ControlWindow::drainPendingVideoFrame()
{
    QImage cpuFrame;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    rtc::D3D11VideoFrame d3d11Frame;
#endif
    int frameKind = 0;
    {
        QMutexLocker locker(&m_videoFrameMutex);
        frameKind = m_pendingVideoFrameKind;
        m_pendingVideoFrameKind = 0;
        m_videoFrameDrainScheduled = false;
        if (frameKind == kPendingCpuFrame)
            cpuFrame.swap(m_pendingVideoFrame);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        else if (frameKind == kPendingD3D11Frame)
        {
            d3d11Frame = std::move(m_pendingD3D11VideoFrame);
            m_pendingD3D11VideoFrame = rtc::D3D11VideoFrame{};
        }
#endif
    }

    if (isClosing())
        return;
    if (frameKind == kPendingCpuFrame)
        updateImg(cpuFrame);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    else if (frameKind == kPendingD3D11Frame)
        updateD3D11Frame(d3d11Frame);
#endif
}


void ControlWindow::updateImg(const QImage &img)
{
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
    {
        LOG_WARN("Received invalid image: null={}, size={}x{}",
                 img.isNull(), img.width(), img.height());
        return;
    }

    switchToCpuVideoWidget();

    if (!isReceivedImg)
        markConnectionWaitingFrameDone();
    isReceivedImg = true;
    if (m_remoteResolution != img.size())
    {
        m_remoteResolution = img.size();
        refreshStatsLabel();
        updateAndroidSidePanelWidth();
        updateToolbarPosition();
        if (windowSizeAdjusted)
            windowSizeAdjusted = false;
    }

    if (!m_fpsTimer.isValid())
        m_fpsTimer.start();
    ++m_fpsFrameCount;
    const qint64 elapsedMs = m_fpsTimer.elapsed();
    if (elapsedMs >= 1000)
    {
        m_currentFps = (m_fpsFrameCount * 1000.0) / elapsedMs;
        m_fpsFrameCount = 0;
        m_fpsTimer.restart();
        refreshStatsLabel();
    }

    if (!windowSizeAdjusted)
        adjustWindowSizeToVideo(img.size());
    m_windowSize = img.size();

    QPixmap pixmap = QPixmap::fromImage(img, Qt::ColorOnly);
    if (pixmap.isNull())
    {
        LOG_ERROR("Failed to convert QImage to QPixmap, image size: {}x{}, format: {}",
                  img.width(), img.height(), static_cast<int>(img.format()));
        return;
    }

    m_sourcePixmap = pixmap;
    m_cachedScaledTargetSize = QSize();
    m_cachedScaledPixmapSize = QSize();
    if (!m_fitToWindow)
        label.resize(m_sourcePixmap.size());
    updateScaledPixmap();
}
