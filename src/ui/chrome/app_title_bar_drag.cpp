#include "app_title_bar.h"

#include <QMouseEvent>
#include <QPushButton>


void AppTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_targetWindow)
    {
        m_dragging = true;
        m_dragOffset = event->globalPos() - m_targetWindow->frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}


void AppTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && m_targetWindow && (event->buttons() & Qt::LeftButton))
    {
        if (m_targetWindow->isMaximized())
        {
            m_targetWindow->showNormal();
            m_dragOffset = QPoint(m_targetWindow->width() / 2, height() / 2);
        }
        m_targetWindow->move(event->globalPos() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}


void AppTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}


void AppTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_maximizeButton)
    {
        toggleMaximize();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
