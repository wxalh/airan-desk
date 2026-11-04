#include "ui/control/control_window.h"
#include "ui/control/control_window_view_helpers.h"
#include "ui/common/adaptive_ui.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

/*
 * Creates the Android remote-control side navigation panel.
 */
void ControlWindow::createAndroidNavigationPanel()
{
    if (!m_centralHost)
        return;

    if (m_androidNavPanel)
        return;

    m_androidNavHost = new QWidget(m_centralHost);
    m_androidNavHost->setObjectName(QStringLiteral("androidNavHost"));
    m_androidNavHost->setFixedWidth(UiAdaptive::scaled(152));
    m_androidNavHost->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto *hostLayout = new QVBoxLayout(m_androidNavHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    m_androidNavPanel = new QFrame(m_androidNavHost);
    m_androidNavPanel->setObjectName(QStringLiteral("androidNavPanel"));
    m_androidNavPanel->setFrameStyle(static_cast<int>(QFrame::StyledPanel) | static_cast<int>(QFrame::Raised));
    m_androidNavPanel->installEventFilter(this);

    auto *panelLayout = new QVBoxLayout(m_androidNavPanel);
    const int panelMargin = UiAdaptive::scaled(10);
    panelLayout->setContentsMargins(panelMargin, panelMargin, panelMargin, panelMargin);
    panelLayout->setSpacing(UiAdaptive::scaled(6));

    auto *title = new QLabel(tr("Android"), m_androidNavPanel);
    title->setObjectName(QStringLiteral("androidNavTitle"));
    title->setAlignment(Qt::AlignCenter);
    title->installEventFilter(this);
    panelLayout->addWidget(title);

    auto addNavButton = [this, panelLayout](QPushButton *&button, const QString &text, const QString &action, const QString &tip)
    {
        button = new QPushButton(text, m_androidNavPanel);
        button->setObjectName(QStringLiteral("androidNavButton"));
        button->setToolTip(tip);
        fitControlButtonWidthToText(button);
        connect(button, &QPushButton::clicked, this, [this, action]()
                { sendAndroidNavigation(action); });
        panelLayout->addWidget(button);
    };

    addNavButton(m_androidBackBtn, tr("Back"), QStringLiteral("back"), tr("Android Back"));
    addNavButton(m_androidHomeBtn, tr("Home"), QStringLiteral("home"), tr("Android Home"));
    addNavButton(m_androidRecentsBtn, tr("Recents"), QStringLiteral("recents"), tr("Android Recents"));
    panelLayout->addStretch(1);

    m_androidNavHost->setFixedWidth(0);
    m_androidNavHost->hide();
    constrainAndroidNavigationPanel();
}

/*
 * Toggles the Android side navigation panel and updates the window layout.
 */
void ControlWindow::setAndroidNavigationVisible(bool visible)
{
    m_androidNavigationVisible = visible;
    if (m_androidNavHost)
    {
        updateAndroidSidePanelWidth();
        if (visible)
            constrainAndroidNavigationPanel();
    }
    if (m_centralHost)
        m_centralHost->updateGeometry();
    updateToolbarPosition();
    if (isReceivedImg && !m_sourcePixmap.isNull())
    {
        adjustWindowSizeToVideo(m_sourcePixmap.size());
    }
    else
    {
        updateScaledPixmap();
    }
}

/*
 * Keeps the Android navigation panel inside the side bar and away from the floating toolbar.
 */
void ControlWindow::constrainAndroidNavigationPanel()
{
    if (!m_androidNavigationVisible || !m_androidNavPanel || !m_androidNavHost || !m_androidNavHost->isVisible())
        return;

    m_androidNavPanel->adjustSize();
    const int margin = UiAdaptive::scaled(8);
    const int maxX = qMax(0, m_androidNavHost->width() - m_androidNavPanel->width() - margin);
    const int maxY = qMax(0, m_androidNavHost->height() - m_androidNavPanel->height() - margin);

    QPoint target = m_androidNavPanel->pos();
    if (target.isNull())
    {
        const int toolbarBottom = (shouldPlaceToolbarInSidePanel() && m_floatingToolbar)
                                      ? (m_floatingToolbar->y() + m_floatingToolbar->height() + margin)
                                      : margin;
        target = QPoint(maxX, qMin(toolbarBottom, maxY));
    }

    target.setX(qBound(qMin(margin, maxX), target.x(), maxX));
    target.setY(qBound(qMin(margin, maxY), target.y(), maxY));
    if (shouldPlaceToolbarInSidePanel() && m_floatingToolbar)
    {
        const QRect toolbarRect(m_floatingToolbar->pos(), m_floatingToolbar->size());
        const QRect navRect(target, m_androidNavPanel->size());
        if (toolbarRect.intersects(navRect))
            target.setY(qBound(qMin(margin, maxY), m_floatingToolbar->y() + m_floatingToolbar->height() + margin, maxY));
    }
    m_androidNavPanel->move(target);
}
