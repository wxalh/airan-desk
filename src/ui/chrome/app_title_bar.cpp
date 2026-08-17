#include "app_title_bar.h"
#include "app_title_bar_icons.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>


AppTitleBar::AppTitleBar(QWidget *targetWindow, bool showMinimize, bool showMaximize, QWidget *parent)
    : QWidget(parent ? parent : targetWindow),
      m_targetWindow(targetWindow)
{
    setObjectName(QStringLiteral("appTitleBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setFixedHeight(38);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    enableAncestorMouseTracking();

    if (m_targetWindow)
    {
        m_targetWindow->setAttribute(Qt::WA_Hover, true);
        m_targetWindow->setMouseTracking(true);
        m_targetWindow->installEventFilter(this);
    }

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(14, 0, 8, 0);
    m_layout->setSpacing(6);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setObjectName(QStringLiteral("appTitleBarIcon"));
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(QSize(20, 20));
    const QIcon appIcon = (m_targetWindow && !m_targetWindow->windowIcon().isNull())
                              ? m_targetWindow->windowIcon()
                              : qApp->windowIcon();
    m_iconLabel->setPixmap(appIcon.pixmap(QSize(18, 18)));
    m_layout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(m_targetWindow ? m_targetWindow->windowTitle() : QString(), this);
    m_titleLabel->setObjectName(QStringLiteral("appTitleBarTitle"));
    m_titleLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_layout->addWidget(m_titleLabel, 1);

    if (showMinimize)
    {
        m_minimizeButton = createButton(makeTitleBarIcon(TitleBarGlyph::Minimize), tr("Minimize"));
        m_minimizeButton->setObjectName(QStringLiteral("appTitleBarMinimize"));
        connect(m_minimizeButton, &QPushButton::clicked, m_targetWindow, &QWidget::showMinimized);
        m_layout->addWidget(m_minimizeButton);
    }

    if (showMaximize)
    {
        m_maximizeButton = createButton(makeTitleBarIcon(TitleBarGlyph::Maximize), tr("Maximize"));
        m_maximizeButton->setObjectName(QStringLiteral("appTitleBarMaximize"));
        connect(m_maximizeButton, &QPushButton::clicked, this, &AppTitleBar::toggleMaximize);
        m_layout->addWidget(m_maximizeButton);
    }

    m_closeButton = createButton(makeTitleBarIcon(TitleBarGlyph::Close), tr("Close"));
    m_closeButton->setObjectName(QStringLiteral("appTitleBarClose"));
    connect(m_closeButton, &QPushButton::clicked, m_targetWindow, &QWidget::close);
    m_layout->addWidget(m_closeButton);

    applyScale();

    if (m_targetWindow)
    {
        connect(m_targetWindow, &QWidget::windowTitleChanged, this, &AppTitleBar::setTitle);
        qApp->installEventFilter(this);
    }

    // Some Windows/Qt combinations do not deliver hover moves to an
    // inactive frameless window until it receives focus. Polling the live
    // cursor keeps title-bar feedback independent of the activation state.
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setInterval(80);
    connect(m_hoverTimer, &QTimer::timeout, this, &AppTitleBar::syncButtonHoverFromCursor);
    m_hoverTimer->start();
    updateMaximizeButton();
}


AppTitleBar::~AppTitleBar()
{
    if (m_targetWindow)
        m_targetWindow->removeEventFilter(this);
    if (qApp)
        qApp->removeEventFilter(this);
}


void AppTitleBar::setTitle(const QString &title)
{
    if (m_titleLabel)
        m_titleLabel->setText(title);
}


void AppTitleBar::setResizeAspectRatio(const QSize &baseSize)
{
    m_resizeAspectBase = baseSize;
}


bool AppTitleBar::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    if (!event)
        return handled;

    switch (event->type())
    {
    case QEvent::ParentChange:
    case QEvent::Show:
        // The title bar is initially constructed with the window as parent,
        // then reparented by titleBarHost's layout. Refresh the live chain
        // after that reparent so the host does not swallow hover movement.
        enableAncestorMouseTracking();
        break;
    case QEvent::ApplicationFontChange:
    case QEvent::ScreenChangeInternal:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    case QEvent::DevicePixelRatioChange:
#endif
        applyScale();
        break;
    default:
        break;
    }
    return handled;
}


void AppTitleBar::enableAncestorMouseTracking()
{
    // Frameless windows do not get a native title bar to drive mouse
    // tracking. Keep every intermediate container producing hover/move
    // events even when the target window is inactive.
    for (QWidget *widget = this; widget; widget = widget->parentWidget())
    {
        widget->setAttribute(Qt::WA_Hover, true);
        widget->setMouseTracking(true);
        if (widget == m_targetWindow)
            break;
    }
}
