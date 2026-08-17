#include "terminal/emulator/native_terminal_widget.h"

#include <QMetaObject>
#include <QTimerEvent>

namespace
{
constexpr int kScrollBarReservedWidth = 10;
}

void NativeTerminalWidget::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_blinkTimer.timerId())
    {
        m_cursorBlinkState = !m_cursorBlinkState;
        update(cursorPaintRect());
        return;
    }
    if (event->timerId() == m_selectionScrollTimer.timerId())
    {
        if (!m_selecting || m_selectionAutoScrollDirection == 0)
        {
            m_selectionScrollTimer.stop();
            return;
        }

        const int beforeOffset = m_scrollbackOffset;
        scrollHistory(m_selectionAutoScrollDirection);
        if (beforeOffset == m_scrollbackOffset)
            return;

        const int row = m_selectionAutoScrollDirection > 0 ? 0 : m_gridSize.height() - 1;
        updateSelection(QPoint(m_selectionCursor.x(), row));
        return;
    }
    QWidget::timerEvent(event);
}

void NativeTerminalWidget::updateGridFromViewport()
{
    const int cols = qMax(20, qMax(0, width() - kScrollBarReservedWidth) / m_cellWidth);
    const int rows = qMax(5, height() / m_cellHeight);
    if (cols == m_gridSize.width() && rows == m_gridSize.height())
        return;

    m_gridSize = QSize(cols, rows);
    if (m_worker && !m_workerClosing.load())
    {
        QMetaObject::invokeMethod(m_worker, "setGridSize", Qt::QueuedConnection,
                                  Q_ARG(int, cols), Q_ARG(int, rows));
    }
    emit gridSizeChanged(m_gridSize);
}

void NativeTerminalWidget::clearScreenAndScrollback(bool notifyRemote)
{
    m_scrollbackOffset = 0;
    clearSelection();
    updateScrollBar();
    if (m_worker && !m_workerClosing.load())
    {
        QMetaObject::invokeMethod(m_worker, "clearScreenAndScrollback", Qt::QueuedConnection,
                                  Q_ARG(bool, notifyRemote));
    }
}

void NativeTerminalWidget::updateCursorBlink()
{
    if (m_cursorBlink && hasFocus())
    {
        if (!m_blinkTimer.isActive())
            m_blinkTimer.start(530, this);
    }
    else
    {
        m_blinkTimer.stop();
        m_cursorBlinkState = true;
    }
}
