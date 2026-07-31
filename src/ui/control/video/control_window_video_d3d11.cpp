#include "ui/control/control_window.h"

#include "ui/video/d3d11_video_widget.h"

#include <QMetaObject>

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)


void ControlWindow::updateD3D11Frame(const rtc::D3D11VideoFrame &frame)
{
    if (!frame.isValid())
    {
        LOG_WARN("Received invalid D3D11 image: size={}x{}, format={}",
                 frame.width, frame.height, static_cast<int>(frame.format));
        return;
    }

    if (!m_useD3D11Video || !ensureD3D11VideoWidget())
        return;

    switchToD3D11VideoWidget();
    updateD3D11VideoGeometry();
    if (!m_d3d11VideoWidget->setFrame(frame))
    {
        LOG_WARN("D3D11 video widget rejected frame; falling back to CPU video widget: size={}x{}, format={}",
                 frame.width,
                 frame.height,
                 static_cast<int>(frame.format));
        m_useD3D11Video = false;
        switchToCpuVideoWidget();
        QMetaObject::invokeMethod(&m_rtc_ctl,
                                  [ctl = &m_rtc_ctl]() {
                                      ctl->disableD3D11VideoRendering();
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    if (!isReceivedImg)
        markConnectionWaitingFrameDone();
    isReceivedImg = true;

    const QSize frameSize(frame.width, frame.height);
    if (m_remoteResolution != frameSize)
    {
        m_remoteResolution = frameSize;
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
        adjustWindowSizeToVideo(frameSize);
    m_windowSize = frameSize;
    updateD3D11VideoGeometry();
}


bool ControlWindow::ensureD3D11VideoWidget()
{
    if (!m_useD3D11Video)
        return false;
    if (!m_d3d11VideoWidget)
    {
        m_d3d11VideoWidget = new D3D11VideoWidget();
        m_d3d11VideoWidget->setFocusPolicy(Qt::StrongFocus);
        m_d3d11VideoWidget->setMouseTracking(true);
        m_d3d11VideoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    return !m_d3d11VideoWidget->isFailed();
}


void ControlWindow::switchToD3D11VideoWidget()
{
    if (!m_d3d11VideoWidget || scrollArea.widget() == m_d3d11VideoWidget)
        return;
    if (scrollArea.widget() == &label)
        scrollArea.takeWidget();
    scrollArea.setWidget(m_d3d11VideoWidget);
    scrollArea.setWidgetResizable(false);
    m_d3d11VideoWidget->show();
}


void ControlWindow::updateD3D11VideoGeometry()
{
    if (!m_d3d11VideoWidget || !m_d3d11VideoWidget->hasFrame())
        return;

    const QSize sourceSize = m_d3d11VideoWidget->sourceSize();
    if (sourceSize.isEmpty())
        return;

    if (!m_fitToWindow)
    {
        if (m_d3d11VideoWidget->size() != sourceSize)
            m_d3d11VideoWidget->resize(sourceSize);
        m_videoDisplayRect = QRect(QPoint(0, 0), sourceSize);
        return;
    }

    const QSize targetSize = scrollArea.viewport()->size();
    if (targetSize.isEmpty())
        return;

    if (m_d3d11VideoWidget->size() != targetSize)
        m_d3d11VideoWidget->resize(targetSize);
    const QSize scaledSize = sourceSize.scaled(targetSize, Qt::KeepAspectRatio);
    m_videoDisplayRect = QRect(QPoint((targetSize.width() - scaledSize.width()) / 2,
                                      (targetSize.height() - scaledSize.height()) / 2),
                               scaledSize);
    m_d3d11VideoWidget->update();
}
#endif
