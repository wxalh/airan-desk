#include "ui/control/control_window.h"

#include "ui/common/adaptive_ui.h"
#include <QScreen>
#include <QStyle>
#include <QTimer>

/*
 * Updates video statistics and refreshes toolbar/side-panel layout when resolution changes.
 */
void ControlWindow::updateVideoStats(double kbps, const QSize &resolution)
{
    if (isClosing())
        return;
    m_currentKbps = kbps;
    if (resolution.isValid() && m_remoteResolution != resolution)
    {
        m_remoteResolution = resolution;
        updateAndroidSidePanelWidth();
        updateToolbarPosition();
    }
    refreshStatsLabel();
}

void ControlWindow::onRemoteDesktopStateChanged(bool locked, const QString &message)
{
    if (isClosing())
        return;
    m_remoteDesktopLocked = locked;
    Q_UNUSED(message);
    if (locked)
    {
        LOG_INFO("Remote desktop is locked; keeping video active for secure desktop frames");
        return;
    }

    updateScaledPixmap();
    LOG_INFO("Remote desktop is unlocked; normal video rendering resumed");
}

/*
 * Adjusts the control window size from the first video frame and available screen area.
 */
void ControlWindow::adjustWindowSizeToVideo(const QSize &videoSize)
{
    if (isClosing())
        return;
    LOG_DEBUG("Adjusting window size to match video: {}x{}", videoSize.width(), videoSize.height());

    QScreen *screen = UiAdaptive::screenForWidget(this);
    QRect screenGeometry = UiAdaptive::availableGeometry(this);

    LOG_DEBUG("Screen available geometry: {}x{}", screenGeometry.width(), screenGeometry.height());
    const int titleBarHeight = menuWidget() ? menuWidget()->sizeHint().height()
                                            : style()->pixelMetric(QStyle::PM_TitleBarHeight);
    const int sidePanelWidth = (m_androidNavHost && m_androidNavHost->isVisible()) ? m_androidNavHost->width() : 0;
    const int maxContentWidth = qMax(320, screenGeometry.width() - sidePanelWidth);
    const int maxContentHeight = qMax(240, screenGeometry.height() - titleBarHeight);

    const QSize videoDisplaySize = videoSize.scaled(maxContentWidth, maxContentHeight, Qt::KeepAspectRatio);
    QSize initialWindowSize(videoDisplaySize.width() + sidePanelWidth,
                            videoDisplaySize.height() + titleBarHeight);
    initialWindowSize.setWidth(qMin(initialWindowSize.width(), screenGeometry.width()));
    initialWindowSize.setHeight(qMin(initialWindowSize.height(), screenGeometry.height()));

    bool needMaximize = initialWindowSize.height() > screenGeometry.height() ||
                        initialWindowSize.width() > screenGeometry.width();
    if (needMaximize)
        showMaximized();
    else
        resize(initialWindowSize);

    scrollArea.updateGeometry();
    updateGeometry();

    if (!needMaximize && screen)
    {
        QRect windowGeometry = geometry();
        windowGeometry.moveCenter(screenGeometry.center());

        if (windowGeometry.left() < screenGeometry.left())
            windowGeometry.moveLeft(screenGeometry.left());
        if (windowGeometry.top() < screenGeometry.top())
            windowGeometry.moveTop(screenGeometry.top());
        if (windowGeometry.right() > screenGeometry.right())
            windowGeometry.moveRight(screenGeometry.right());
        if (windowGeometry.bottom() > screenGeometry.bottom())
            windowGeometry.moveBottom(screenGeometry.bottom());

        setGeometry(windowGeometry);

        LOG_DEBUG("Window positioned at: ({}, {}), size: {}x{}",
                  windowGeometry.x(), windowGeometry.y(),
                  windowGeometry.width(), windowGeometry.height());
    }

    windowSizeAdjusted = true;
    updateScaledPixmap();
    updateToolbarPosition();
    QTimer::singleShot(0, this, [this]() {
        if (isClosing())
            return;
        updateScaledPixmap();
        updateToolbarPosition();
    });
    QTimer::singleShot(120, this, [this]() {
        if (isClosing())
            return;
        updateScaledPixmap();
        updateToolbarPosition();
    });
}
