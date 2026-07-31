#include "terminal/emulator/native_terminal_widget.h"

#include <QMetaObject>
#include <QMouseEvent>
#include <QWheelEvent>


void NativeTerminalWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);

    if (event->button() == Qt::RightButton)
    {
        showContextMenu(event->globalPos());
        event->accept();
        return;
    }

    if (shouldStartLocalSelection(event))
    {
        beginSelection(cellFromPosition(event->pos()));
        event->accept();
        return;
    }

    sendMouseButton(event, true);
}


void NativeTerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_selecting)
    {
        updateSelection(cellFromPosition(event->pos()));
        finishSelection();
        event->accept();
        return;
    }

    sendMouseButton(event, false);
}


void NativeTerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting)
    {
        const QPoint pos = event->pos();
        const int edge = qMax(1, m_cellHeight);
        if (pos.y() < edge)
            m_selectionAutoScrollDirection = 1;
        else if (pos.y() >= height() - edge)
            m_selectionAutoScrollDirection = -1;
        else
            m_selectionAutoScrollDirection = 0;

        if (m_selectionAutoScrollDirection != 0 && !m_selectionScrollTimer.isActive())
            m_selectionScrollTimer.start(55, this);
        else if (m_selectionAutoScrollDirection == 0)
            m_selectionScrollTimer.stop();

        updateSelection(cellFromPosition(pos));
        event->accept();
        return;
    }

    sendMouseMove(event);
}


void NativeTerminalWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_worker || m_workerClosing.load())
    {
        QWidget::wheelEvent(event);
        return;
    }

    const int steps = wheelSteps(event);
    const bool forceLocalScroll = event->modifiers().testFlag(Qt::ShiftModifier);
    if (!forceLocalScroll && steps != 0 &&
        m_alternateScreen && m_mouseMode == VTERM_PROP_MOUSE_NONE)
    {
        sendWheelCursorKeys(steps);
        event->accept();
        return;
    }

    if (m_mouseMode == VTERM_PROP_MOUSE_NONE || forceLocalScroll)
    {
        if (steps != 0)
        {
            scrollHistory(steps * 3);
            event->accept();
            return;
        }
        event->accept();
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QPoint cell = cellFromPosition(event->position().toPoint());
#else
    const QPoint cell = cellFromPosition(event->pos());
#endif
    const int modifiers = static_cast<int>(mouseModifiersFromQt(event->modifiers()));
    QMetaObject::invokeMethod(m_worker, "sendMouseMove", Qt::QueuedConnection,
                              Q_ARG(int, cell.y()), Q_ARG(int, cell.x()), Q_ARG(int, modifiers));
    if (steps == 0)
    {
        event->accept();
        return;
    }
    const int button = steps > 0 ? 4 : 5;
    QMetaObject::invokeMethod(m_worker, "sendMouseButton", Qt::QueuedConnection,
                              Q_ARG(int, button), Q_ARG(bool, true), Q_ARG(int, modifiers));
    QMetaObject::invokeMethod(m_worker, "sendMouseButton", Qt::QueuedConnection,
                              Q_ARG(int, button), Q_ARG(bool, false), Q_ARG(int, modifiers));
    event->accept();
}


int NativeTerminalWidget::wheelSteps(QWheelEvent *event)
{
    const int angleSteps = event->angleDelta().y() / 120;
    if (angleSteps != 0)
    {
        m_wheelPixelRemainder = 0;
        return angleSteps;
    }

    m_wheelPixelRemainder += event->pixelDelta().y();
    const int cellHeight = qMax(1, m_cellHeight);
    const int pixelSteps = m_wheelPixelRemainder / cellHeight;
    m_wheelPixelRemainder -= pixelSteps * cellHeight;
    return pixelSteps;
}


void NativeTerminalWidget::sendWheelCursorKeys(int steps)
{
    const VTermKey key = steps > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN;
    for (int remaining = qAbs(steps); remaining > 0; --remaining)
        sendKey(key, VTERM_MOD_NONE);
}


void NativeTerminalWidget::sendMouseButton(QMouseEvent *event, bool pressed)
{
    if (!m_worker || m_workerClosing.load() || m_mouseMode == VTERM_PROP_MOUSE_NONE)
    {
        event->ignore();
        return;
    }

    int button = 0;
    if (event->button() == Qt::LeftButton)
        button = 1;
    else if (event->button() == Qt::MiddleButton)
        button = 2;
    else if (event->button() == Qt::RightButton)
        button = 3;
    else
        return;

    const QPoint cell = cellFromPosition(event->pos());
    const int modifiers = static_cast<int>(mouseModifiersFromQt(event->modifiers()));
    QMetaObject::invokeMethod(m_worker, "sendMouseMove", Qt::QueuedConnection,
                              Q_ARG(int, cell.y()), Q_ARG(int, cell.x()), Q_ARG(int, modifiers));
    QMetaObject::invokeMethod(m_worker, "sendMouseButton", Qt::QueuedConnection,
                              Q_ARG(int, button), Q_ARG(bool, pressed), Q_ARG(int, modifiers));
    event->accept();
}


void NativeTerminalWidget::sendMouseMove(QMouseEvent *event)
{
    if (!m_worker || m_workerClosing.load() || m_mouseMode == VTERM_PROP_MOUSE_NONE)
        return;

    const QPoint cell = cellFromPosition(event->pos());
    QMetaObject::invokeMethod(m_worker, "sendMouseMove", Qt::QueuedConnection,
                              Q_ARG(int, cell.y()), Q_ARG(int, cell.x()),
                              Q_ARG(int, static_cast<int>(mouseModifiersFromQt(event->modifiers()))));
}


QPoint NativeTerminalWidget::cellFromPosition(const QPoint &pos) const
{
    const int col = qBound(0, pos.x() / m_cellWidth, m_gridSize.width() - 1);
    const int row = qBound(0, pos.y() / m_cellHeight, m_gridSize.height() - 1);
    return QPoint(col, row);
}


void NativeTerminalWidget::scrollHistory(int lines)
{
    const int scrollbackLines = m_snapshot ? m_snapshot->scrollbackLines : 0;
    if (scrollbackLines <= 0 || lines == 0)
        return;

    const int newOffset = qBound(0, m_scrollbackOffset + lines, scrollbackLines);
    if (newOffset == m_scrollbackOffset)
        return;

    setScrollbackOffset(newOffset);
}
