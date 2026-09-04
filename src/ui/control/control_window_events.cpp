#include "ui/control/control_window.h"

#include "ui/video/d3d11_video_widget.h"

#include <QApplication>
#include <QEvent>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>


void ControlWindow::closeEvent(QCloseEvent *event)
{
    if (m_shutdownComplete)
    {
        event->accept();
        return;
    }
    event->ignore();
    beginAsyncShutdown();
}

void ControlWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateScaledPixmap();
    updateToolbarPosition();
    constrainAndroidNavigationPanel();
}


QPointF ControlWindow::getNormPoint(const QPoint &pos)
{
    bool hasD3D11Frame = false;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    hasD3D11Frame = m_d3d11VideoWidget && m_d3d11VideoWidget->hasFrame();
#endif

    if ((m_sourcePixmap.isNull() && !hasD3D11Frame) || m_videoDisplayRect.isEmpty())
        return QPointF(-1.0, -1.0);

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    QWidget *videoWidget = (m_d3d11VideoWidget && scrollArea.widget() == m_d3d11VideoWidget)
                               ? static_cast<QWidget *>(m_d3d11VideoWidget)
                               : static_cast<QWidget *>(&label);
#else
    QWidget *videoWidget = &label;
#endif
    const QPoint widgetPos = videoWidget->mapFrom(scrollArea.viewport(), scrollArea.viewport()->mapFrom(this, pos));
    if (!m_videoDisplayRect.contains(widgetPos))
        return QPointF(-1.0, -1.0);

    const qreal x = (widgetPos.x() - m_videoDisplayRect.x()) / static_cast<qreal>(m_videoDisplayRect.width());
    const qreal y = (widgetPos.y() - m_videoDisplayRect.y()) / static_cast<qreal>(m_videoDisplayRect.height());
    return QPointF(qBound(0.0, x, 1.0), qBound(0.0, y, 1.0));
}


bool ControlWindow::isValidNormPoint(const QPointF &pos) const
{
    return pos.x() >= 0.0 && pos.x() <= 1.0 && pos.y() >= 0.0 && pos.y() <= 1.0;
}


bool ControlWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event && (event->type() == QEvent::WindowDeactivate ||
                  event->type() == QEvent::ApplicationDeactivate))
    {
        releaseRemotePressedKeys();
    }
    else if (!m_remotePressedKeys.isEmpty() && !shouldCaptureRemoteKeyboard())
    {
        releaseRemotePressedKeys();
    }

    if (handleToolbarAutoHideEvent(watched, event))
        return true;
    if (handleToolbarDragEvent(watched, event))
        return true;
    if (handleAndroidNavigationDragEvent(watched, event))
        return true;
    if (handleRemoteMouseMoveEvent(watched, event))
        return true;
    if (event && shouldCaptureRemoteKeyboard())
    {
        if (event->type() == QEvent::ShortcutOverride)
        {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::KeyPress)
            return handleRemoteKeyboardEvent(static_cast<QKeyEvent *>(event), true);
        if (event->type() == QEvent::KeyRelease)
            return handleRemoteKeyboardEvent(static_cast<QKeyEvent *>(event), false);
    }
    return QMainWindow::eventFilter(watched, event);
}


bool ControlWindow::handleRemoteMouseMoveEvent(QObject *watched, QEvent *event)
{
    if (!event || event->type() != QEvent::MouseMove)
        return false;

    if (m_draggingToolbar || m_draggingAndroidNav)
        return false;

    QWidget *watchedWidget = qobject_cast<QWidget *>(watched);
    bool isVideoTarget = watchedWidget == &label || watchedWidget == scrollArea.viewport();
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    isVideoTarget = isVideoTarget || watchedWidget == m_d3d11VideoWidget;
#endif
    if (!isVideoTarget)
        return false;

    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    sendRemoteMouseMoveAt(mapFromGlobal(mouseEvent->globalPos()));
    mouseEvent->accept();
    return true;
}


bool ControlWindow::handleToolbarDragEvent(QObject *watched, QEvent *event)
{
    const bool toolbarDragTarget = watched == m_floatingToolbar ||
                                   watched == m_statsLabel ||
                                   watched == m_toolbarButtonRow;
    if (!toolbarDragTarget || !m_floatingToolbar)
        return false;

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            m_draggingToolbar = true;
            m_dragStartPosition = mouseEvent->globalPos();
            m_toolbarOffset = mouseEvent->globalPos() - m_floatingToolbar->mapToGlobal(QPoint(0, 0));
            mouseEvent->accept();
            return true;
        }
    }
    else if (event->type() == QEvent::MouseMove && m_draggingToolbar)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        QWidget *toolbarParent = m_floatingToolbar->parentWidget();
        if (!toolbarParent)
            toolbarParent = this;

        QPoint target = toolbarParent->mapFromGlobal(mouseEvent->globalPos() - m_toolbarOffset);
        const int maxX = qMax(0, toolbarParent->width() - m_floatingToolbar->width());
        const int maxY = qMax(0, toolbarParent->height() - m_floatingToolbar->height());
        target.setX(qBound(0, target.x(), maxX));
        target.setY(qBound(0, target.y(), maxY));
        m_floatingToolbar->move(target);
        m_toolbarUserMoved = true;
        mouseEvent->accept();
        return true;
    }
    else if (event->type() == QEvent::MouseButtonRelease && m_draggingToolbar)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            m_draggingToolbar = false;
            m_toolbarUserMoved = true;
            mouseEvent->accept();
            return true;
        }
    }

    return false;
}


bool ControlWindow::handleAndroidNavigationDragEvent(QObject *watched, QEvent *event)
{
    const bool navDragTarget = watched == m_androidNavPanel ||
                               (m_androidNavPanel && qobject_cast<QWidget *>(watched) &&
                                qobject_cast<QWidget *>(watched)->parentWidget() == m_androidNavPanel);

    if (!navDragTarget || !m_androidNavPanel || !m_androidNavHost)
        return false;

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            m_draggingAndroidNav = true;
            m_androidNavDragStart = mouseEvent->globalPos();
            m_androidNavStartPos = m_androidNavPanel->pos();
            mouseEvent->accept();
            return true;
        }
    }
    else if (event->type() == QEvent::MouseMove && m_draggingAndroidNav)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint delta = mouseEvent->globalPos() - m_androidNavDragStart;
        QPoint target = m_androidNavStartPos + delta;
        const int maxX = qMax(0, m_androidNavHost->width() - m_androidNavPanel->width());
        const int maxY = qMax(0, m_androidNavHost->height() - m_androidNavPanel->height());
        target.setX(qBound(0, target.x(), maxX));
        target.setY(qBound(0, target.y(), maxY));
        m_androidNavPanel->move(target);
        mouseEvent->accept();
        return true;
    }
    else if (event->type() == QEvent::MouseButtonRelease && m_draggingAndroidNav)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            m_draggingAndroidNav = false;
            mouseEvent->accept();
            return true;
        }
    }

    return false;
}
