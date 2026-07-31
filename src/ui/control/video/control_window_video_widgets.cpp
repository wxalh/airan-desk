#include "ui/control/control_window.h"

#include "ui/video/d3d11_video_widget.h"
#include "ui/video/video_cpu_render_util.h"

#include <QLabel>


void ControlWindow::switchToCpuVideoWidget()
{
    if (scrollArea.widget() == &label)
        return;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (m_d3d11VideoWidget && scrollArea.widget() == static_cast<QWidget *>(m_d3d11VideoWidget))
        scrollArea.takeWidget();
#endif
    scrollArea.setWidget(&label);
    scrollArea.setWidgetResizable(false);
    label.show();
}


void ControlWindow::updateScaledPixmap()
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (m_d3d11VideoWidget && scrollArea.widget() == m_d3d11VideoWidget && m_d3d11VideoWidget->hasFrame())
    {
        updateD3D11VideoGeometry();
        return;
    }
#endif

    if (m_sourcePixmap.isNull())
    {
        m_videoDisplayRect = QRect();
        m_scaledPixmap = QPixmap();
        m_cachedScaledTargetSize = QSize();
        m_cachedScaledPixmapSize = QSize();
        renderConnectionProgress();
        return;
    }

    if (!m_fitToWindow)
    {
        const QSize sourceSize = m_sourcePixmap.size();
        if (sourceSize.isEmpty())
            return;

        if (label.size() != sourceSize)
            label.resize(sourceSize);

        m_videoDisplayRect = QRect(QPoint(0, 0), sourceSize);
        m_scaledPixmap = QPixmap();
        m_cachedScaledTargetSize = QSize();
        m_cachedScaledPixmapSize = QSize();
        label.setPixmap(m_sourcePixmap);
        label.update();
        return;
    }

    const QSize targetSize = scrollArea.viewport()->size();
    if (targetSize.isEmpty())
        return;

    label.resize(targetSize);

    const QSize scaledSize = m_sourcePixmap.size().scaled(targetSize, Qt::KeepAspectRatio);
    if (scaledSize.isEmpty())
        return;

    if (m_cachedScaledTargetSize != targetSize ||
        m_cachedScaledPixmapSize != m_sourcePixmap.size() ||
        m_scaledPixmap.isNull())
    {
        m_scaledPixmap = VideoCpuRenderUtil::scalePixmap(m_sourcePixmap, scaledSize);
        if (m_scaledPixmap.isNull())
            m_scaledPixmap = m_sourcePixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::FastTransformation);
        m_cachedScaledTargetSize = targetSize;
        m_cachedScaledPixmapSize = m_sourcePixmap.size();
    }

    m_videoDisplayRect = QRect(QPoint((targetSize.width() - scaledSize.width()) / 2,
                                      (targetSize.height() - scaledSize.height()) / 2),
                               scaledSize);
    label.setPixmap(m_scaledPixmap);
    label.update();
}
