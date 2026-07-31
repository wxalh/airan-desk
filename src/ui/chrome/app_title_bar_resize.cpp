#include "app_title_bar.h"

#include <QEvent>
#include <QMouseEvent>


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
    Q_UNUSED(watched);
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
