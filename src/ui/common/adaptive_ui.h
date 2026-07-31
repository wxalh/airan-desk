#ifndef AIRAN_DESK_ADAPTIVE_UI_H
#define AIRAN_DESK_ADAPTIVE_UI_H

#include <QFontMetrics>
#include <QGuiApplication>
#include <QHeaderView>
#include <QPair>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QVector>
#include <QWidget>
#include <QWindow>

namespace UiAdaptive
{
    
    static inline int textWidth(const QFontMetrics &metrics, const QString &text)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
        return metrics.horizontalAdvance(text);
#else
        return metrics.width(text);
#endif
    }

    
    inline QScreen *screenForWidget(QWidget *widget)
    {
        if (widget)
        {
            if (QWindow *handle = widget->windowHandle())
            {
                if (QScreen *screen = handle->screen())
                    return screen;
            }
            if (QWidget *window = widget->window())
            {
                if (QWindow *handle = window->windowHandle())
                {
                    if (QScreen *screen = handle->screen())
                        return screen;
                }
            }
        }
        return QGuiApplication::primaryScreen();
    }

    
    inline QRect availableGeometry(QWidget *widget)
    {
        QScreen *screen = screenForWidget(widget);
        return screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    }

    
    inline double proportionalScale(QWidget *widget, const QSize &baseSize,
                                    double screenRatio = 0.92,
                                    double maxScale = 1.35,
                                    double minScale = 0.45)
    {
        if (baseSize.isEmpty())
            return 1.0;

        const QRect available = availableGeometry(widget);
        const QSize limit(qMax(1, static_cast<int>(available.width() * screenRatio)),
                          qMax(1, static_cast<int>(available.height() * screenRatio)));
        const double scale = qMin(limit.width() / static_cast<double>(baseSize.width()),
                                  limit.height() / static_cast<double>(baseSize.height()));
        return qBound(minScale, scale, maxScale);
    }

    
    inline QSize scaledSize(const QSize &baseSize, double scale)
    {
        return QSize(qMax(1, static_cast<int>(qRound(baseSize.width() * scale))),
                     qMax(1, static_cast<int>(qRound(baseSize.height() * scale))));
    }

    
    inline QSize applyAdaptiveWindowSize(QWidget *window,
                                         const QSize &baseSize,
                                         const QSize &preferredMinimum = QSize(360, 260),
                                         double screenRatio = 0.92,
                                         double maxScale = 1.35)
    {
        if (!window)
            return baseSize;

        const QRect available = availableGeometry(window);
        const QSize limit(qMax(1, static_cast<int>(available.width() * screenRatio)),
                          qMax(1, static_cast<int>(available.height() * screenRatio)));
        const double scale = proportionalScale(window, baseSize, screenRatio, maxScale);
        QSize target = scaledSize(baseSize, scale);
        target.setWidth(qMin(target.width(), limit.width()));
        target.setHeight(qMin(target.height(), limit.height()));

        const QSize adaptiveMinimum(qMax(1, qMin(preferredMinimum.width(), limit.width())),
                                    qMax(1, qMin(preferredMinimum.height(), limit.height())));
        window->setMinimumSize(adaptiveMinimum);
        window->resize(target.expandedTo(adaptiveMinimum));
        return window->size();
    }

    
    inline void makeTableAdaptive(QTableWidget *table,
                                  const QVector<int> &stretchColumns = QVector<int>(),
                                  const QVector<QPair<int, int>> &fixedWidths = QVector<QPair<int, int>>(),
                                  bool stretchLast = true)
    {
        if (!table)
            return;

        auto header = table->horizontalHeader();
        header->setSectionResizeMode(QHeaderView::Interactive);
        header->setStretchLastSection(stretchLast);
        header->setMinimumSectionSize(28);
        table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

        if (stretchColumns.isEmpty())
        {
            if (table->columnCount() > 0)
                header->setSectionResizeMode(0, QHeaderView::Stretch);
        }
        else
        {
            for (int col : stretchColumns)
            {
                if (col >= 0 && col < table->columnCount())
                    header->setSectionResizeMode(col, QHeaderView::Stretch);
            }
        }

        for (const auto &p : fixedWidths)
        {
            const int col = p.first;
            const int w = p.second;
            if (col >= 0 && col < table->columnCount())
                table->setColumnWidth(col, w);
        }
    }

    
    inline void setAdaptiveMinWidth(QWidget *w, const QString &text, int padding = 24)
    {
        if (!w)
            return;
        QFontMetrics fm(w->font());
        int wreq = textWidth(fm, text) + padding;
        const int minAdaptive = qMax(120, wreq);
        w->setMinimumWidth(minAdaptive);
    }

    
    inline void fitButtonWidthToText(QPushButton *button)
    {
        if (!button)
            return;

        button->ensurePolished();

        QFontMetrics metrics(button->font());
        QStyleOptionButton option;
        option.initFrom(button);
        option.text = button->text();
        option.icon = button->icon();
        option.iconSize = button->iconSize();

        QSize contentSize(textWidth(metrics, button->text()), metrics.height());
        const int width = button->style()->sizeFromContents(QStyle::CT_PushButton, &option, contentSize, button).width();
        button->setFixedWidth(width);
    }

} /* namespace UiAdaptive */

#endif /* AIRAN_DESK_ADAPTIVE_UI_H */
