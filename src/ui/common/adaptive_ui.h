#ifndef AIRAN_DESK_ADAPTIVE_UI_H
#define AIRAN_DESK_ADAPTIVE_UI_H

#include <QFontMetrics>
#include <QGuiApplication>
#include <QApplication>
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

    inline double scaleFactor()
    {
        const QVariant value = QApplication::instance()
                                   ? QApplication::instance()->property("airanUiScaleFactor") : QVariant();
        const double scale = value.isValid() ? value.toDouble() : 1.0;
        return qIsFinite(scale) ? qBound(0.8, scale, 1.4) : 1.0;
    }

    inline int scaled(int value)
    {
        return qMax(1, qRound(value * scaleFactor()));
    }

    inline QSize scaledSizeForFactor(const QSize &baseSize, double scale)
    {
        return QSize(qMax(1, qRound(baseSize.width() * scale)),
                     qMax(1, qRound(baseSize.height() * scale)));
    }

    inline QSize scaledSize(const QSize &baseSize, double factor)
    {
        return scaledSizeForFactor(baseSize, scaleFactor() * factor);
    }
    
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
        for (QWidget *current = widget; current; current = current->parentWidget())
        {
            if (QWindow *handle = current->windowHandle())
            {
                if (QScreen *screen = handle->screen())
                    return screen;
            }
            if (QWidget *window = current->window())
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

    inline QRect goldenWindowGeometry(const QRect &available, const QSize &minimum)
    {
        constexpr double goldenFraction = 0.61803398875;
        const QSize limit(qMax(1, available.width() - 32), qMax(1, available.height() - 32));
        const QSize preferred(qRound(available.width() * goldenFraction),
                              qRound(available.height() * goldenFraction));
        const QSize target = preferred.expandedTo(minimum).boundedTo(limit);
        QRect geometry(QPoint(), target);
        geometry.moveCenter(available.center());
        return geometry;
    }

    inline QRect preferredWindowGeometry(const QRect &available, const QSize &preferred, const QSize &minimum)
    {
        const QSize limit(qMax(1, available.width() - 32), qMax(1, available.height() - 32));
        QSize target = preferred.expandedTo(minimum);
        if (target.width() > limit.width() || target.height() > limit.height())
            target.scale(limit, Qt::KeepAspectRatio);
        QRect geometry(QPoint(), target.expandedTo(QSize(1, 1)));
        geometry.moveCenter(available.center());
        return geometry;
    }

    inline QSize applyPreferredWindowSize(QWidget *window, const QSize &preferred,
                                           const QSize &preferredMinimum)
    {
        if (!window)
            return preferred;
        const QRect available = availableGeometry(window);
        const QRect geometry = preferredWindowGeometry(available, scaledSize(preferred, 1.0),
                                                         scaledSize(preferredMinimum, 1.0));
        window->setMinimumSize(scaledSize(preferredMinimum, 1.0).boundedTo(geometry.size()));
        window->setGeometry(geometry);
        return window->size();
    }

    inline void centerWindowOnScreen(QWidget *window)
    {
        if (!window || window->isMaximized() || window->isFullScreen())
            return;
        const QRect available = availableGeometry(window);
        QRect frame = window->frameGeometry();
        frame.moveCenter(available.center());
        window->move(window->pos() + frame.topLeft() - window->frameGeometry().topLeft());
    }

    inline QSize applyGoldenWindowSize(QWidget *window, const QSize &preferredMinimum)
    {
        if (!window)
            return preferredMinimum;
        const QRect available = availableGeometry(window);
        const QSize limit(qMax(1, available.width() - 32), qMax(1, available.height() - 32));
        const QSize minimum = scaledSize(preferredMinimum, 1.0).boundedTo(limit);
        window->setMinimumSize(minimum);
        window->setGeometry(goldenWindowGeometry(available, minimum));
        return window->size();
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

    
    inline QSize applyAdaptiveWindowSize(QWidget *window,
                                         const QSize &baseSize,
                                         const QSize &preferredMinimum = QSize(360, 260),
                                         double screenRatio = 0.92,
                                         double maxScale = 1.35,
                                         double externalScale = 1.0)
    {
        if (!window)
            return baseSize;

        const QRect available = availableGeometry(window);
        const QSize limit(qMax(1, static_cast<int>(available.width() * screenRatio)),
                          qMax(1, static_cast<int>(available.height() * screenRatio)));
        const double effectiveScale = scaleFactor() * externalScale;
        const QSize scaledBase = scaledSizeForFactor(baseSize, effectiveScale);
        const double scale = proportionalScale(window, scaledBase, screenRatio, maxScale);
        QSize target = scaledSizeForFactor(scaledBase, scale);
        target.setWidth(qMin(target.width(), limit.width()));
        target.setHeight(qMin(target.height(), limit.height()));

        const QSize scaledMinimum = scaledSizeForFactor(preferredMinimum, effectiveScale);
        const QSize adaptiveMinimum(qMax(1, qMin(scaledMinimum.width(), limit.width())),
                                    qMax(1, qMin(scaledMinimum.height(), limit.height())));
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
        header->setMinimumSectionSize(scaled(28));
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
                table->setColumnWidth(col, scaled(w));
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
