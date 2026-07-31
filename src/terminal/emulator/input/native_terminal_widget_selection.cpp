#include "terminal/emulator/native_terminal_widget.h"

#include <QMouseEvent>
#include <algorithm>


bool NativeTerminalWidget::shouldStartLocalSelection(QMouseEvent *event) const
{
    if (event->button() != Qt::LeftButton)
        return false;
    return m_mouseMode == VTERM_PROP_MOUSE_NONE || event->modifiers().testFlag(Qt::ShiftModifier);
}


void NativeTerminalWidget::beginSelection(const QPoint &cell)
{
    m_selecting = true;
    m_hasSelection = true;
    m_selectionAutoScrollDirection = 0;
    m_selectionAnchor = globalCellFromVisibleCell(cell);
    m_selectionCursor = m_selectionAnchor;
    update();
}


void NativeTerminalWidget::updateSelection(const QPoint &cell)
{
    const QPoint globalCell = globalCellFromVisibleCell(cell);
    if (m_selectionCursor == globalCell)
        return;

    m_selectionCursor = globalCell;
    m_hasSelection = true;
    update();
}


void NativeTerminalWidget::finishSelection()
{
    m_selecting = false;
    m_selectionAutoScrollDirection = 0;
    m_selectionScrollTimer.stop();
    if (m_selectionAnchor == m_selectionCursor)
        m_hasSelection = false;
    update();
}


void NativeTerminalWidget::clearSelection()
{
    if (!m_hasSelection && !m_selecting)
        return;

    m_selecting = false;
    m_selectionAutoScrollDirection = 0;
    m_selectionScrollTimer.stop();
    m_hasSelection = false;
    update();
}


bool NativeTerminalWidget::hasSelection() const
{
    return m_hasSelection && m_selectionAnchor != m_selectionCursor;
}


bool NativeTerminalWidget::isCellSelected(int row, int col) const
{
    if (!hasSelection())
        return false;

    QPoint start = m_selectionAnchor;
    QPoint end = m_selectionCursor;
    if (isCellBefore(end, start))
        std::swap(start, end);

    const int globalLine = visibleGlobalLine(row);
    if (globalLine < start.y() || globalLine > end.y())
        return false;
    if (globalLine == start.y() && col < start.x())
        return false;
    if (globalLine == end.y() && col > end.x())
        return false;
    return true;
}


bool NativeTerminalWidget::isCellRangeSelected(int row, int col, int width) const
{
    const int lastCol = col + qMax(1, width) - 1;
    for (int currentCol = col; currentCol <= lastCol; ++currentCol)
    {
        if (isCellSelected(row, currentCol))
            return true;
    }
    return false;
}


QString NativeTerminalWidget::selectedText() const
{
    if (!m_snapshot || !hasSelection())
        return QString();

    QPoint start = m_selectionAnchor;
    QPoint end = m_selectionCursor;
    if (isCellBefore(end, start))
        std::swap(start, end);

    QString result;
    VTermScreenCell cell{};
    for (int globalLine = start.y(); globalLine <= end.y(); ++globalLine)
    {
        QString line;
        const int startCol = globalLine == start.y() ? start.x() : 0;
        const int endCol = globalLine == end.y() ? end.x() : m_gridSize.width() - 1;
        for (int col = startCol; col <= endCol; ++col)
        {
            if (!globalCell(globalLine, col, &cell))
                continue;
            if (cell.width == 0 || isWideCharTrailingCell(cell))
                continue;
            line += cellText(cell);
        }

        result += trimTrailingSpaces(line);
        if (globalLine != end.y())
            result += QLatin1Char('\n');
    }
    return result;
}
