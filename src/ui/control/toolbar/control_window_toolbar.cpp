#include "ui/control/control_window.h"

#include "ui/common/adaptive_ui.h"

#include <QFrame>
#include <QApplication>
#include <QCursor>
#include <QMouseEvent>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>


void ControlWindow::createFloatingToolbar()
{
    setupFloatingToolbarFrame();

    QVBoxLayout *mainLayout = new QVBoxLayout(m_floatingToolbar);
    mainLayout->setSpacing(UiAdaptive::scaled(5));
    mainLayout->setContentsMargins(UiAdaptive::scaled(12), UiAdaptive::scaled(8),
                                   UiAdaptive::scaled(12), UiAdaptive::scaled(8));

    createToolbarStatsLabel(mainLayout);
    createToolbarButtonRow();
    createToolbarCoreButtons();
    createToolbarModeMenus();

    m_toolbarScrollArea = new QScrollArea(m_floatingToolbar);
    m_toolbarScrollArea->setObjectName(QStringLiteral("remoteToolbarScrollArea"));
    m_toolbarScrollArea->setFrameShape(QFrame::NoFrame);
    m_toolbarScrollArea->setWidgetResizable(false);
    m_toolbarScrollArea->setWidget(m_toolbarButtonRow);
    m_toolbarScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mainLayout->addWidget(m_toolbarScrollArea);

    m_floatingToolbar->setMouseTracking(true);
    m_floatingToolbar->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_floatingToolbar->installEventFilter(this);
    const QList<QWidget *> toolbarChildren = m_floatingToolbar->findChildren<QWidget *>();
    for (QWidget *child : toolbarChildren)
        child->setMouseTracking(true);
    m_statsLabel->installEventFilter(this);
    m_toolbarButtonRow->installEventFilter(this);
    m_toolbarScrollArea->viewport()->installEventFilter(this);

    updateToolbarPosition();
    m_toolbarShownPosition = m_floatingToolbar->pos();
}


void ControlWindow::setupFloatingToolbarFrame()
{
    m_floatingToolbar = new QFrame(this);
    m_floatingToolbar->setObjectName(QStringLiteral("remoteControlToolbar"));
    m_floatingToolbar->setFrameStyle(static_cast<int>(QFrame::StyledPanel) | static_cast<int>(QFrame::Raised));
    m_floatingToolbar->hide();


    m_toolbarAutoHideTimer = new QTimer(this);
    m_toolbarAutoHideTimer->setSingleShot(true);
    connect(m_toolbarAutoHideTimer, &QTimer::timeout, this, [this]() {
        if (!isReceivedImg || isClosing() || !m_floatingToolbar || m_draggingToolbar)
            return;
        if (QApplication::activePopupWidget())
        {
            m_toolbarAutoHideTimer->start(3000);
            return;
        }
        const QRect toolbarRect(m_floatingToolbar->mapToGlobal(QPoint(0, 0)),
                                m_floatingToolbar->size());
        if (toolbarRect.contains(QCursor::pos()))
        {
            m_toolbarAutoHideTimer->start(3000);
            return;
        }
        setToolbarAutoHidden(true);
    });
}

bool ControlWindow::handleToolbarAutoHideEvent(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)
    if (!event || !isReceivedImg || !m_floatingToolbar || !m_toolbarAutoHideTimer || isClosing())
        return false;

    QPoint globalPos;
    if (event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease)
    {
        globalPos = static_cast<QMouseEvent *>(event)->globalPos();
    }
    else if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
    {
        globalPos = QCursor::pos();
    }
    else
    {
        return false;
    }

    const QRect toolbarRect(m_floatingToolbar->mapToGlobal(QPoint(0, 0)),
                            m_floatingToolbar->size());
    if (m_toolbarAutoHidden)
    {
        const int revealMargin = UiAdaptive::scaled(14);
        if (toolbarRect.adjusted(-revealMargin, -revealMargin, revealMargin, revealMargin).contains(globalPos))
        {
            setToolbarAutoHidden(false);
            m_toolbarAutoHideTimer->start(3000);
        }
        return false;
    }

    if (toolbarRect.contains(globalPos))
        m_toolbarAutoHideTimer->stop();
    else if (!m_draggingToolbar && !m_toolbarAutoHideTimer->isActive())
        m_toolbarAutoHideTimer->start(3000);
    return false;
}

void ControlWindow::setToolbarAutoHidden(bool hidden)
{
    if (!isReceivedImg || isClosing())
    {
        if (m_floatingToolbar)
            m_floatingToolbar->hide();
        if (m_toolbarAutoHideTimer)
            m_toolbarAutoHideTimer->stop();
        return;
    }
    if (!m_floatingToolbar || m_toolbarAutoHidden == hidden)
        return;
    QWidget *parent = m_floatingToolbar->parentWidget();
    if (!parent)
        return;

    if (!hidden)
    {
        m_toolbarAutoHidden = false;
        m_floatingToolbar->show();
        m_floatingToolbar->move(m_toolbarShownPosition);
        m_floatingToolbar->raise();
        m_toolbarAutoHideTimer->start(3000);
        return;
    }

    m_toolbarShownPosition = m_floatingToolbar->pos();
    m_toolbarAutoHidden = true;
    applyToolbarAutoHiddenPosition();
    m_toolbarAutoHideTimer->stop();
}

void ControlWindow::applyToolbarAutoHiddenPosition()
{
    if (!isReceivedImg || isClosing() || !m_floatingToolbar || !m_toolbarAutoHidden)
        return;
    QWidget *parent = m_floatingToolbar->parentWidget();
    if (!parent)
        return;

    const int visibleGrip = UiAdaptive::scaled(8);
    const QPoint shown = m_toolbarShownPosition;
    const int left = qMax(0, shown.x());
    const int top = qMax(0, shown.y());
    const int right = qMax(0, parent->width() - shown.x() - m_floatingToolbar->width());
    const int bottom = qMax(0, parent->height() - shown.y() - m_floatingToolbar->height());
    const int nearest = qMin(qMin(left, right), qMin(top, bottom));
    QPoint hidden = shown;
    if (nearest == top)
        hidden.setY(-m_floatingToolbar->height() + visibleGrip);
    else if (nearest == bottom)
        hidden.setY(parent->height() - visibleGrip);
    else if (nearest == left)
        hidden.setX(-m_floatingToolbar->width() + visibleGrip);
    else
        hidden.setX(parent->width() - visibleGrip);
    m_floatingToolbar->move(hidden);
    m_floatingToolbar->raise();
}
