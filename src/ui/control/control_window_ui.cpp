#include "ui/control/control_window.h"

#include "common/logger_manager.h"
#include "ui/common/adaptive_ui.h"
#include "ui/chrome/app_title_bar.h"
#include "ui_control_window.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>

/*
 * Initializes the control window UI, title bar, video area, and Android navigation host.
 */
void ControlWindow::initUI()
{
    ui = new Ui::ControlWindow();
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    setWindowTitle(tr("Remote: %1").arg(remote_id));
    setMenuWidget(new AppTitleBar(this, true, true, this));
    resize(800, 600);

    label.clear();
    UiAdaptive::applyAdaptiveWindowSize(this, QSize(800, 600), QSize(320, 240));
    label.setAlignment(Qt::AlignCenter);
    label.setWordWrap(true);
    label.setMouseTracking(true);
    label.setFocusPolicy(Qt::StrongFocus);
    label.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    label.setStyleSheet("QLabel { background: black; border: none; margin: 0px; padding: 0px; color: white; font-size: 16px; }");

    scrollArea.setWidget(&label);
    scrollArea.setWidgetResizable(false);
    LOG_INFO("Initialized with QLabel video rendering, window size will auto-adjust to video");

    scrollArea.setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea.setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea.setStyleSheet(
        "QScrollArea {"
        "    border: none;"
        "    background: black;"
        "    margin: 0px;"
        "    padding: 0px;"
        "}"
        "QScrollArea > QWidget > QWidget {"
        "    background: black;"
        "}"
        "QScrollBar:vertical {"
        "    background: rgba(0,0,0,0);"
        "    width: 8px;"
        "    border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: rgba(128,128,128,0.5);"
        "    border-radius: 4px;"
        "    min-height: 20px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: rgba(128,128,128,0.8);"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "    border: none;"
        "    background: none;"
        "    height: 0px;"
        "}"
        "QScrollBar:horizontal {"
        "    background: rgba(0,0,0,0);"
        "    height: 8px;"
        "    border-radius: 4px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "    background: rgba(128,128,128,0.5);"
        "    border-radius: 4px;"
        "    min-width: 20px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "    background: rgba(128,128,128,0.8);"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "    border: none;"
        "    background: none;"
        "    width: 0px;"
        "}");

    scrollArea.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scrollArea.setFocusPolicy(Qt::StrongFocus);
    scrollArea.viewport()->setFocusPolicy(Qt::StrongFocus);
    scrollArea.setMouseTracking(true);
    scrollArea.viewport()->setMouseTracking(true);
    scrollArea.setAlignment(Qt::AlignCenter);
    scrollArea.setContentsMargins(0, 0, 0, 0);
    scrollArea.setFrameShape(QFrame::NoFrame);
    scrollArea.setLineWidth(0);
    scrollArea.setMidLineWidth(0);
    label.setContentsMargins(0, 0, 0, 0);

    m_centralHost = new QWidget(this);
    auto *centralLayout = new QHBoxLayout(m_centralHost);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(&scrollArea, 1);

    setCentralWidget(m_centralHost);
    m_centralHost->setMouseTracking(true);
    createAndroidNavigationPanel();
    if (m_androidNavHost)
        centralLayout->addWidget(m_androidNavHost);

    setContentsMargins(0, 0, 0, 0);
    centralWidget()->setContentsMargins(0, 0, 0, 0);
    if (QWidget *titleBar = menuWidget())
    {
        titleBar->setMouseTracking(true);
        const QList<QWidget *> titleBarChildren = titleBar->findChildren<QWidget *>();
        for (QWidget *child : titleBarChildren)
            child->setMouseTracking(true);
    }
    qApp->installEventFilter(this);
    setFocus(Qt::OtherFocusReason);
}
