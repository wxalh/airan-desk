#ifndef TERMINAL_TOOLBAR_FLOW_LAYOUT_H
#define TERMINAL_TOOLBAR_FLOW_LAYOUT_H

#include <QLayout>
#include <QList>
#include <QStyle>
#include <QWidget>

// Keep the existing actions in order, wrapping only when their real sizes do
// not fit. Both the parent layout's height measurement and placement use the
// same calculation, so widening the splitter restores the single row.
class TerminalToolbarFlowLayout final : public QLayout
{
public:
    explicit TerminalToolbarFlowLayout(int spacing)
    {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(spacing);
    }

    ~TerminalToolbarFlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0))
            delete item;
    }

    void addItem(QLayoutItem *item) override
    {
        m_items.append(item);
        invalidate();
    }

    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        if (index < 0 || index >= m_items.size())
            return nullptr;
        QLayoutItem *item = m_items.takeAt(index);
        invalidate();
        return item;
    }

    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        return arrange(QRect(0, 0, width, 0), false);
    }

    QSize minimumSize() const override
    {
        QSize result(0, 0);
        for (QLayoutItem *item : m_items)
            if (!item->isEmpty())
                result = result.expandedTo(item->minimumSize());
        const QMargins margins = contentsMargins();
        return result + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

    QSize sizeHint() const override
    {
        QSize result(0, 0);
        int count = 0;
        for (QLayoutItem *item : m_items)
        {
            if (item->isEmpty())
                continue;
            result.rwidth() += item->sizeHint().width();
            result.setHeight(qMax(result.height(), item->sizeHint().height()));
            ++count;
        }
        result.rwidth() += qMax(0, count - 1) * qMax(0, spacing());
        const QMargins margins = contentsMargins();
        return result + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

private:
    int arrange(const QRect &rect, bool place) const
    {
        const QMargins margins = contentsMargins();
        const QRect available = rect.marginsRemoved(margins);
        const int gap = qMax(0, spacing());
        const Qt::LayoutDirection direction = parentWidget() ? parentWidget()->layoutDirection() : Qt::LeftToRight;
        int x = available.left();
        int y = available.top();
        int rowHeight = 0;
        bool rowStarted = false;
        for (QLayoutItem *item : m_items)
        {
            if (item->isEmpty())
                continue;
            const QSize size = item->sizeHint();
            if (rowStarted && x + size.width() > available.right() + 1)
            {
                x = available.left();
                y += rowHeight + gap;
                rowHeight = 0;
                rowStarted = false;
            }
            if (place)
                item->setGeometry(QStyle::visualRect(direction, available, QRect(QPoint(x, y), size)));
            x += size.width() + gap;
            rowHeight = qMax(rowHeight, size.height());
            rowStarted = true;
        }
        return y + rowHeight - rect.top() + margins.bottom();
    }

    QList<QLayoutItem *> m_items;
};

#endif
