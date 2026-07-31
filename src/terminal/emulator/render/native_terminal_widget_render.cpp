#include "terminal/emulator/native_terminal_widget.h"
#include "terminal/emulator/native_terminal_widget_colors.h"

#include <QPainter>
#include <QPaintEvent>
#include <algorithm>

namespace
{
QFont fontForCell(const QFont &baseFont, const VTermScreenCell &cell)
{
    QFont font = baseFont;
    font.setBold(cell.attrs.bold);
    font.setItalic(cell.attrs.italic);
    font.setStrikeOut(cell.attrs.strike);
    font.setUnderline(cell.attrs.underline != VTERM_UNDERLINE_OFF);
    return font;
}

void drawCellText(QPainter &painter,
                  const QRect &cellRect,
                  int ascent,
                  const QString &text,
                  const QFont &font,
                  const QColor &color)
{
    if (text.isEmpty())
        return;

    painter.save();
    painter.setClipRect(cellRect);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(cellRect.left(), cellRect.top() + ascent, text);
    painter.restore();
}
}


void NativeTerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), kDefaultBackground);
    painter.setFont(m_font);

    const int firstRow = qMax(0, event->rect().top() / m_cellHeight);
    const int lastRow = qMin(m_gridSize.height() - 1, event->rect().bottom() / m_cellHeight);
    VTermScreenCell cell{};

    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int col = 0; col < m_gridSize.width(); ++col)
        {
            if (!visibleCell(row, col, &cell))
                continue;
            if (cell.width == 0 || isWideCharTrailingCell(cell))
                continue;

            QColor fg = colorFromVTerm(cell.fg, true);
            QColor bg = colorFromVTerm(cell.bg, false);
            if (cell.attrs.reverse)
                std::swap(fg, bg);
            if (cell.attrs.conceal)
                fg = bg;

            const QRect cellRect(col * m_cellWidth, row * m_cellHeight, m_cellWidth * qMax(1, static_cast<int>(cell.width)), m_cellHeight);
            if (isCellRangeSelected(row, col, qMax(1, static_cast<int>(cell.width))))
                bg = kSelectionBackground;
            painter.fillRect(cellRect, bg);
        }

        for (int col = 0; col < m_gridSize.width(); ++col)
        {
            if (!visibleCell(row, col, &cell))
                continue;
            if (cell.width == 0)
                continue;

            QColor fg = colorFromVTerm(cell.fg, true);
            QColor bg = colorFromVTerm(cell.bg, false);
            if (cell.attrs.reverse)
                std::swap(fg, bg);
            if (cell.attrs.conceal)
                fg = bg;

            const QString text = cellText(cell);
            if (!text.isEmpty())
            {
                const QRect cellRect(col * m_cellWidth, row * m_cellHeight, m_cellWidth * qMax(1, static_cast<int>(cell.width)), m_cellHeight);
                if (isCellRangeSelected(row, col, qMax(1, static_cast<int>(cell.width))))
                    fg = kSelectionForeground;
                drawCellText(painter, cellRect, m_ascent, text, fontForCell(m_font, cell), fg);
            }
        }
    }

    if (m_scrollbackOffset == 0 && hasFocus() && m_cursorVisible && m_cursorBlinkState)
    {
        const QRect cursorRect(m_cursorPos.col * m_cellWidth, m_cursorPos.row * m_cellHeight, m_cellWidth, m_cellHeight);
        if (m_cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE)
        {
            painter.fillRect(QRect(cursorRect.left(), cursorRect.bottom() - 2, cursorRect.width(), 3), kCursorColor);
        }
        else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT)
        {
            painter.fillRect(QRect(cursorRect.left(), cursorRect.top(), qMax(2, cursorRect.width() / 6), cursorRect.height()), kCursorColor);
        }
        else
        {
            VTermScreenCell cursorCell{};
            const bool hasCursorCell = visibleCell(m_cursorPos.row, m_cursorPos.col, &cursorCell) &&
                                       !isWideCharTrailingCell(cursorCell);
            const int cursorWidth = hasCursorCell ? qMax(1, static_cast<int>(cursorCell.width)) : 1;
            const QRect blockCursorRect(cursorRect.left(), cursorRect.top(),
                                        cursorRect.width() * cursorWidth, cursorRect.height());
            QColor cursorTextColor = kDefaultBackground;
            if (hasCursorCell)
            {
                QColor cellForeground = colorFromVTerm(cursorCell.fg, true);
                QColor cellBackground = colorFromVTerm(cursorCell.bg, false);
                if (cursorCell.attrs.reverse)
                    std::swap(cellForeground, cellBackground);
                cursorTextColor = cellBackground;
            }

            painter.fillRect(blockCursorRect, kCursorColor);
            if (hasCursorCell && !cursorCell.attrs.conceal)
            {
                drawCellText(painter, blockCursorRect, m_ascent, cellText(cursorCell),
                             fontForCell(m_font, cursorCell), cursorTextColor);
            }
        }
    }
}


bool NativeTerminalWidget::visibleCell(int row, int col, VTermScreenCell *cell) const
{
    if (!cell)
        return false;

    return globalCell(visibleGlobalLine(row), col, cell);
}


bool NativeTerminalWidget::globalCell(int globalLine, int col, VTermScreenCell *cell) const
{
    if (!cell || !m_snapshot)
        return false;
    const int cols = m_snapshot->gridSize.width();
    const int totalRows = m_snapshot->scrollbackLines + m_snapshot->gridSize.height();
    if (globalLine < 0 || globalLine >= totalRows || col < 0 || col >= cols)
        return false;
    const qint64 index = static_cast<qint64>(globalLine) * cols + col;
    if (index < 0 || index >= m_snapshot->cells.size())
        return false;
    *cell = m_snapshot->cells.at(static_cast<int>(index));
    return true;
}


int NativeTerminalWidget::visibleGlobalLine(int row) const
{
    const int scrollbackLines = m_snapshot ? m_snapshot->scrollbackLines : 0;
    const int totalRows = scrollbackLines + m_gridSize.height();
    if (totalRows <= 0)
        return 0;

    const int startLine = qMax(0, totalRows - m_gridSize.height() - m_scrollbackOffset);
    return qBound(0, startLine + row, totalRows - 1);
}


QPoint NativeTerminalWidget::globalCellFromVisibleCell(const QPoint &cell) const
{
    return QPoint(qBound(0, cell.x(), qMax(0, m_gridSize.width() - 1)), visibleGlobalLine(cell.y()));
}


QColor NativeTerminalWidget::colorFromVTerm(VTermColor color, bool foreground) const
{
    if (foreground && VTERM_COLOR_IS_DEFAULT_FG(&color))
        return kDefaultForeground;
    if (!foreground && VTERM_COLOR_IS_DEFAULT_BG(&color))
        return kDefaultBackground;

    return QColor(color.rgb.red, color.rgb.green, color.rgb.blue);
}


QString NativeTerminalWidget::cellText(const VTermScreenCell &cell) const
{
    if (isWideCharTrailingCell(cell))
        return QString();

    QVector<uint> chars;
    chars.reserve(VTERM_MAX_CHARS_PER_CELL);
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
        chars.append(cell.chars[i]);
    return chars.isEmpty() ? QString() : QString::fromUcs4(chars.constData(), chars.size());
}
