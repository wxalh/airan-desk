#include "ui/control/control_window.h"

#include <QWidget>


void ControlWindow::updateToolbarPosition()
{
    if (!m_floatingToolbar)
        return;

    const bool sidePanelMode = shouldPlaceToolbarInSidePanel();
    const bool parentChanged = sidePanelMode != m_toolbarInSidePanel;

    applyToolbarLayoutMode(sidePanelMode);
    updateAndroidSidePanelWidth();

    QWidget *targetParent = sidePanelMode ? m_androidNavHost : static_cast<QWidget *>(this);
    if (targetParent && m_floatingToolbar->parentWidget() != targetParent)
    {
        const QPoint globalTopLeft = m_floatingToolbar->mapToGlobal(QPoint(0, 0));
        m_floatingToolbar->setParent(targetParent);
        m_floatingToolbar->move(targetParent->mapFromGlobal(globalTopLeft));
        m_floatingToolbar->show();
    }

    m_toolbarInSidePanel = sidePanelMode;
    m_floatingToolbar->adjustSize();

    QWidget *toolbarParent = m_floatingToolbar->parentWidget();
    if (!toolbarParent)
        toolbarParent = this;

    const int availableToolbarWidth = qMax(160, toolbarParent->width() - 16);
    if (!sidePanelMode)
    {
        m_floatingToolbar->setMaximumWidth(availableToolbarWidth);
        if (m_statsLabel)
            m_statsLabel->setMaximumWidth(availableToolbarWidth - 16);
        if (m_toolbarButtonRow)
            m_toolbarButtonRow->setMaximumWidth(availableToolbarWidth - 16);
        m_floatingToolbar->adjustSize();
    }
    else
    {
        m_floatingToolbar->setMaximumWidth(QWIDGETSIZE_MAX);
    }

    QPoint target = m_floatingToolbar->pos();
    if (parentChanged || !m_toolbarUserMoved)
    {
        if (sidePanelMode)
        {
            const int margin = 8;
            const int gap = 12;
            int groupHeight = m_floatingToolbar->height();
            if (m_androidNavigationVisible && m_androidNavPanel)
            {
                m_androidNavPanel->adjustSize();
                groupHeight += gap + m_androidNavPanel->height();
            }
            target = QPoint(qMax(0, (toolbarParent->width() - m_floatingToolbar->width()) / 2),
                            qMax(margin, (toolbarParent->height() - groupHeight) / 2));
        }
        else
        {
            target = QPoint(qMax(0, (toolbarParent->width() - m_floatingToolbar->width()) / 2), 10);
        }
    }

    const int maxX = qMax(0, toolbarParent->width() - m_floatingToolbar->width());
    const int maxY = qMax(0, toolbarParent->height() - m_floatingToolbar->height());
    target.setX(qBound(0, target.x(), maxX));
    target.setY(qBound(0, target.y(), maxY));
    m_floatingToolbar->move(target);
    m_floatingToolbar->raise();

    if (sidePanelMode && m_androidNavigationVisible && m_androidNavPanel && (parentChanged || !m_toolbarUserMoved))
    {
        const int gap = 12;
        const int maxNavX = qMax(0, toolbarParent->width() - m_androidNavPanel->width());
        const int maxNavY = qMax(0, toolbarParent->height() - m_androidNavPanel->height());
        QPoint navTarget(qBound(0, (toolbarParent->width() - m_androidNavPanel->width()) / 2, maxNavX),
                         qBound(0, m_floatingToolbar->y() + m_floatingToolbar->height() + gap, maxNavY));
        m_androidNavPanel->move(navTarget);
    }
    if (sidePanelMode)
        constrainAndroidNavigationPanel();

    if (m_toolbarAutoHidden)
    {
        m_toolbarShownPosition.setX(qBound(0, m_toolbarShownPosition.x(),
                                           qMax(0, toolbarParent->width() - m_floatingToolbar->width())));
        m_toolbarShownPosition.setY(qBound(0, m_toolbarShownPosition.y(),
                                           qMax(0, toolbarParent->height() - m_floatingToolbar->height())));
        applyToolbarAutoHiddenPosition();
    }
}
