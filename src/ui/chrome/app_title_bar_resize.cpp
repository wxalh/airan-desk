#include "app_title_bar.h"

#include <QCursor>
#include <QEvent>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QList>
#include <QPushButton>
#include <QStyle>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QEnterEvent>
#endif


namespace
{
bool isTitleBarButton(const AppTitleBar *titleBar, QObject *watched)
{
    if (!titleBar)
        return false;

    auto *widget = qobject_cast<QWidget *>(watched);
    return widget && (widget == titleBar->findChild<QPushButton *>(QStringLiteral("appTitleBarMinimize")) ||
                      widget == titleBar->findChild<QPushButton *>(QStringLiteral("appTitleBarMaximize")) ||
                      widget == titleBar->findChild<QPushButton *>(QStringLiteral("appTitleBarClose")));
}

QPushButton *titleBarButton(const AppTitleBar *titleBar, QObject *watched)
{
    if (!titleBar)
        return nullptr;

    auto *button = qobject_cast<QPushButton *>(watched);
    return button && isTitleBarButton(titleBar, watched) ? button : nullptr;
}

void setTitleBarButtonHover(QWidget *widget, bool hovered)
{
    if (!widget || widget->property("titleBarHover").toBool() == hovered)
        return;

    widget->setProperty("titleBarHover", hovered);
    if (widget->style())
    {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
    widget->update();
}

void updateTitleBarButtonHover(AppTitleBar *titleBar, const QPoint &globalPos)
{
    if (!titleBar)
        return;

    // Mouse events can carry a stale/translated global position while a
    // frameless window is being moved or activated. Use the button's global
    // geometry as the source of truth so hover feedback does not depend on
    // keyboard focus or a platform widget hit-test succeeding for an inactive
    // window.
    QWidget *window = titleBar->window();
    const bool eventPositionValid = window && window->isVisible() &&
                                    window->rect().contains(window->mapFromGlobal(globalPos));
    const bool cursorPositionValid = window && window->isVisible() &&
                                     window->rect().contains(window->mapFromGlobal(QCursor::pos()));
    const QList<QPushButton *> buttons = titleBar->findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
    {
        if (!isTitleBarButton(titleBar, button))
            continue;

        const bool eventHover = button->rect().contains(button->mapFromGlobal(globalPos));
        const bool cursorHover = button->rect().contains(button->mapFromGlobal(QCursor::pos()));
        const bool hoveredByEvent = eventPositionValid && eventHover;
        const bool hoveredByCursor = cursorPositionValid && cursorHover;
        const bool hovered = button->isVisible() && (hoveredByEvent || hoveredByCursor);
        setTitleBarButtonHover(button, hovered);
    }
}

QPoint titleBarEventGlobalPos(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || !event)
        return QCursor::pos();

    switch (event->type())
    {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
#else
        return static_cast<QMouseEvent *>(event)->globalPos();
#endif
    case QEvent::HoverEnter:
    case QEvent::HoverMove:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return widget->mapToGlobal(static_cast<QHoverEvent *>(event)->position().toPoint());
#else
        return widget->mapToGlobal(static_cast<QHoverEvent *>(event)->pos());
#endif
    case QEvent::Enter:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return widget->mapToGlobal(static_cast<QEnterEvent *>(event)->position().toPoint());
#else
        return QCursor::pos();
#endif
    default:
        return QCursor::pos();
    }
}

}

void AppTitleBar::syncButtonHoverFromCursor()
{
    updateTitleBarButtonHover(this, QCursor::pos());
}


Qt::Edges AppTitleBar::hitTest(const QPoint &globalPos) const
{
    if (!m_targetWindow || !m_targetWindow->isVisible() || m_targetWindow->isMaximized())
        return Qt::Edges();

    const QRect rect = m_targetWindow->frameGeometry();
    if (!rect.adjusted(-4, -4, 4, 4).contains(globalPos))
        return Qt::Edges();

    constexpr int kBorder = 6;
    const bool nearX = globalPos.x() >= rect.left() - kBorder && globalPos.x() <= rect.right() + kBorder;
    const bool nearY = globalPos.y() >= rect.top() - kBorder && globalPos.y() <= rect.bottom() + kBorder;
    Qt::Edges edges;
    if (nearY && qAbs(globalPos.x() - rect.left()) <= kBorder)
        edges |= Qt::LeftEdge;
    if (nearY && qAbs(globalPos.x() - rect.right()) <= kBorder)
        edges |= Qt::RightEdge;
    if (nearX && qAbs(globalPos.y() - rect.top()) <= kBorder)
        edges |= Qt::TopEdge;
    if (nearX && qAbs(globalPos.y() - rect.bottom()) <= kBorder)
        edges |= Qt::BottomEdge;
    return edges;
}


void AppTitleBar::updateCursor(const QPoint &globalPos)
{
    if (!m_targetWindow || m_resizing || m_dragging)
        return;

    const Qt::Edges edges = hitTest(globalPos);
    Qt::CursorShape shape = Qt::ArrowCursor;
    if ((edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge)) ||
        (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge)))
    {
        shape = Qt::SizeFDiagCursor;
    }
    else if ((edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::TopEdge)) ||
             (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::BottomEdge)))
    {
        shape = Qt::SizeBDiagCursor;
    }
    else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge))
    {
        shape = Qt::SizeHorCursor;
    }
    else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge))
    {
        shape = Qt::SizeVerCursor;
    }

    if (shape == Qt::ArrowCursor)
        m_targetWindow->unsetCursor();
    else
        m_targetWindow->setCursor(shape);
}


QRect AppTitleBar::applyResizeAspectRatio(const QRect &geometry, const QPoint &globalPos) const
{
    Q_UNUSED(globalPos);
    if (!m_targetWindow || m_resizeAspectBase.isEmpty() || m_resizeAspectBase.height() <= 0)
        return geometry;

    const QSize minSize = m_targetWindow->minimumSize();
    const double aspect = m_resizeAspectBase.width() / static_cast<double>(m_resizeAspectBase.height());
    QRect adjusted = geometry;
    int width = qMax(minSize.width(), adjusted.width());
    int height = qMax(minSize.height(), static_cast<int>(qRound(width / aspect)));
    if (height < minSize.height())
    {
        height = minSize.height();
        width = qMax(minSize.width(), static_cast<int>(qRound(height * aspect)));
    }

    if (m_resizeEdges.testFlag(Qt::LeftEdge) && !m_resizeEdges.testFlag(Qt::RightEdge))
        adjusted.setLeft(adjusted.right() - width + 1);
    else
        adjusted.setWidth(width);

    if (m_resizeEdges.testFlag(Qt::TopEdge) && !m_resizeEdges.testFlag(Qt::BottomEdge))
        adjusted.setTop(adjusted.bottom() - height + 1);
    else
        adjusted.setHeight(height);

    if (m_resizeEdges == Qt::TopEdge || m_resizeEdges == Qt::BottomEdge)
    {
        height = qMax(minSize.height(), geometry.height());
        width = qMax(minSize.width(), static_cast<int>(qRound(height * aspect)));
        adjusted = geometry;
        if (m_resizeEdges.testFlag(Qt::TopEdge))
            adjusted.setTop(adjusted.bottom() - height + 1);
        else
            adjusted.setHeight(height);

        adjusted.moveLeft(geometry.center().x() - width / 2);
        adjusted.setWidth(width);
    }

    return adjusted;
}


bool AppTitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event && m_targetWindow && watched == m_targetWindow &&
        event->type() == QEvent::WindowActivate)
    {
        // Activation can replay the first mouse move with stale coordinates;
        // synchronize from the live cursor immediately after the window gets
        // focus so the button does not wait for a click to repaint.
        updateTitleBarButtonHover(this, QCursor::pos());
    }

    if (event && (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove ||
                  event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter ||
                  event->type() == QEvent::Leave || event->type() == QEvent::HoverLeave) &&
        m_targetWindow && m_targetWindow->isVisible())
    {
        auto *eventWidget = qobject_cast<QWidget *>(watched);
        if (eventWidget && (eventWidget == m_targetWindow || m_targetWindow->isAncestorOf(eventWidget)))
        {
            updateTitleBarButtonHover(this, titleBarEventGlobalPos(watched, event));
        }
    }

    if (event && isTitleBarButton(this, watched))
    {
        auto *button = titleBarButton(this, watched);
        switch (event->type())
        {
        case QEvent::Enter:
        case QEvent::HoverEnter:
            // Use the event target's state directly. Reading QCursor::pos()
            // during an Enter event can still return the previous position on
            // Qt 5, leaving the hover property false until the next click.
            setTitleBarButtonHover(button, true);
            return QWidget::eventFilter(watched, event);
        case QEvent::Leave:
        case QEvent::HoverLeave:
            setTitleBarButtonHover(button, false);
            return QWidget::eventFilter(watched, event);
        case QEvent::HoverMove:
        case QEvent::MouseMove:
            // Use the event position consistently. During event-filter
            // dispatch Qt may still expose the previous underMouse state;
            // using it here can undo the coordinate-based hover update above.
            updateTitleBarButtonHover(this, titleBarEventGlobalPos(watched, event));
            return QWidget::eventFilter(watched, event);
        default:
            break;
        }
    }

    if (!m_targetWindow || !m_targetWindow->isVisible())
        return QWidget::eventFilter(watched, event);

    if (event->type() != QEvent::MouseMove &&
        event->type() != QEvent::MouseButtonPress &&
        event->type() != QEvent::MouseButtonRelease)
    {
        return QWidget::eventFilter(watched, event);
    }

    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    const QPoint globalPos = mouseEvent->globalPos();

    if (event->type() == QEvent::MouseButtonPress && mouseEvent->button() == Qt::LeftButton)
    {
        m_resizeEdges = hitTest(globalPos);
        if (!m_resizeEdges)
            return QWidget::eventFilter(watched, event);

        QWidget *eventWidget = qobject_cast<QWidget *>(watched);
        if (eventWidget && eventWidget != m_targetWindow && !m_targetWindow->isAncestorOf(eventWidget))
            return QWidget::eventFilter(watched, event);

        m_resizing = true;
        mouseEvent->accept();
        return true;
    }

    if (event->type() == QEvent::MouseMove)
    {
        if (m_resizing && (mouseEvent->buttons() & Qt::LeftButton))
        {
            QRect geometry = m_targetWindow->geometry();
            const QSize minSize = m_targetWindow->minimumSize();
            if (m_resizeEdges.testFlag(Qt::LeftEdge))
            {
                int left = qMin(globalPos.x(), geometry.right() - minSize.width() + 1);
                geometry.setLeft(left);
            }
            if (m_resizeEdges.testFlag(Qt::RightEdge))
            {
                int right = qMax(globalPos.x(), geometry.left() + minSize.width() - 1);
                geometry.setRight(right);
            }
            if (m_resizeEdges.testFlag(Qt::TopEdge))
            {
                int top = qMin(globalPos.y(), geometry.bottom() - minSize.height() + 1);
                geometry.setTop(top);
            }
            if (m_resizeEdges.testFlag(Qt::BottomEdge))
            {
                int bottom = qMax(globalPos.y(), geometry.top() + minSize.height() - 1);
                geometry.setBottom(bottom);
            }
            m_targetWindow->setGeometry(applyResizeAspectRatio(geometry, globalPos));
            mouseEvent->accept();
            return true;
        }
        updateCursor(globalPos);
    }

    if (event->type() == QEvent::MouseButtonRelease && m_resizing)
    {
        m_resizing = false;
        m_resizeEdges = Qt::Edges();
        updateCursor(globalPos);
        mouseEvent->accept();
        return true;
    }

    return QWidget::eventFilter(watched, event);
}
