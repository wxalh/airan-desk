#include "ui/control/control_window.h"

#include <QWidget>
#include <QScrollBar>
#include <QStyle>
#include "ui/common/adaptive_ui.h"
#include "ui/control/control_window_view_helpers.h"


void ControlWindow::updateToolbarPosition()
{
    if (!m_floatingToolbar || !m_toolbarScrollArea || !m_toolbarButtonRow)
        return;

    if (!isReceivedImg || isClosing())
    {
        m_floatingToolbar->hide();
        if (m_toolbarAutoHideTimer)
            m_toolbarAutoHideTimer->stop();
        m_draggingToolbar = false;
        m_draggingAndroidNav = false;
        updateAndroidSidePanelWidth();
        return;
    }

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

    const int margin = UiAdaptive::scaled(8);
    const int availableToolbarWidth = qMax(1, toolbarParent->width() - 2 * margin);
    const QSize rowSize = m_toolbarButtonRow->sizeHint();
    const int scrollbarExtent = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    const QMargins layoutMargins = m_floatingToolbar->layout()->contentsMargins();
    const QMargins frameMargins = m_floatingToolbar->contentsMargins();
    const int horizontalInsets = layoutMargins.left() + layoutMargins.right() +
                                 frameMargins.left() + frameMargins.right();

    // Preserve the two explicit statistics lines whenever the window has room.
    // QLabel's word-wrapped sizeHint is already narrowed, so measure each line
    // before applying the available-width limit.
    m_statsLabel->ensurePolished();
    const QFontMetrics metrics = m_statsLabel->fontMetrics();
    int statsWidth = 0;
    for (const QString &line : m_statsLabel->text().split(QLatin1Char('\n')))
        statsWidth = qMax(statsWidth, qMax(controlWindowTextWidth(metrics, line),
                                          metrics.boundingRect(line).width()));
    const QMargins statsMargins = m_statsLabel->contentsMargins();
    statsWidth += statsMargins.left() + statsMargins.right() + 2 * m_statsLabel->margin();
    const int desiredWidth = sidePanelMode ? rowSize.width() + scrollbarExtent + horizontalInsets
        : qMax(qMax(UiAdaptive::scaled(440), rowSize.width()), statsWidth) + horizontalInsets;
    const int toolbarWidth = qMin(availableToolbarWidth, desiredWidth);
    const int contentWidth = qMax(1, toolbarWidth - horizontalInsets);
    m_statsLabel->setFixedWidth(contentWidth);
    // A previous fixed height becomes QLabel's minimum height; clear it before
    // measuring so a wider window can return to the original two lines.
    m_statsLabel->setMinimumHeight(0);
    m_statsLabel->setMaximumHeight(QWIDGETSIZE_MAX);
    const int statsHeight = qMax(m_statsLabel->fontMetrics().height(),
        m_statsLabel->heightForWidth(contentWidth));
    m_statsLabel->setFixedHeight(qMin(statsHeight, qMax(UiAdaptive::scaled(40), toolbarParent->height() / 3)));
    const int reservedNavigation = sidePanelMode && m_androidNavigationVisible && m_androidNavPanel
        ? m_androidNavPanel->sizeHint().height() + UiAdaptive::scaled(12) : 0;
    const int availableScrollHeight = qMax(UiAdaptive::scaled(30),
        toolbarParent->height() - reservedNavigation - m_statsLabel->maximumHeight() - 5 * margin);
    const int horizontalBarHeight = rowSize.width() > contentWidth ? scrollbarExtent : 0;
    const int scrollHeight = qMin(rowSize.height() + horizontalBarHeight, availableScrollHeight);
    m_toolbarScrollArea->setFixedSize(contentWidth, scrollHeight);
    m_floatingToolbar->setFixedWidth(toolbarWidth);
    m_floatingToolbar->adjustSize();

    QPoint target = m_floatingToolbar->pos();
    if (parentChanged || !m_toolbarUserMoved)
    {
        if (sidePanelMode)
        {
            const int gap = UiAdaptive::scaled(12);
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
            target = QPoint(qMax(0, (toolbarParent->width() - m_floatingToolbar->width()) / 2), UiAdaptive::scaled(10));
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
        const int gap = UiAdaptive::scaled(12);
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
