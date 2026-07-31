#include "ui/control/control_window.h"

#include <QFrame>
#include <QVBoxLayout>


void ControlWindow::createFloatingToolbar()
{
    setupFloatingToolbarFrame();

    QVBoxLayout *mainLayout = new QVBoxLayout(m_floatingToolbar);
    mainLayout->setSpacing(5);
    mainLayout->setContentsMargins(24, 7, 24, 8);

    createToolbarStatsLabel(mainLayout);
    createToolbarButtonRow();
    createToolbarCoreButtons();
    createToolbarModeMenus();

    mainLayout->addWidget(m_toolbarButtonRow, 0, Qt::AlignHCenter);

    m_floatingToolbar->setMouseTracking(true);
    m_floatingToolbar->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_floatingToolbar->installEventFilter(this);
    m_statsLabel->installEventFilter(this);
    m_toolbarButtonRow->installEventFilter(this);

    updateToolbarPosition();
    m_floatingToolbar->raise();
    m_floatingToolbar->show();
}


void ControlWindow::setupFloatingToolbarFrame()
{
    m_floatingToolbar = new QFrame(this);
    m_floatingToolbar->setFrameStyle(static_cast<int>(QFrame::StyledPanel) | static_cast<int>(QFrame::Raised));
    m_floatingToolbar->setStyleSheet(
        "QFrame { background-color: rgba(38,38,38,242); border: 1px solid rgba(80,80,80,180); border-radius: 8px; }"
        "QLabel { color: rgba(255,255,255,230); background: transparent; border: none; padding: 0px 4px; font-size: 10px; }"
        "QPushButton { background-color: rgba(60,60,60,210); border: 1px solid rgba(105,105,105,170); border-radius: 4px; color: white; padding: 5px 12px; margin: 0px; font-size: 12px; }"
        "QPushButton:hover { background-color: rgba(82,82,82,230); border: 1px solid rgba(135,135,135,200); }"
        "QPushButton:pressed { background-color: rgba(48,48,48,245); }"
        "QToolButton { background-color: rgba(60,60,60,210); border: 1px solid rgba(105,105,105,170); border-radius: 4px; color: white; padding: 5px 18px 5px 10px; margin: 0px; font-size: 12px; }"
        "QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; right: 5px; width: 9px; }"
        "QToolButton::menu-button { border: none; width: 16px; }"
        "QToolButton:hover { background-color: rgba(82,82,82,230); border: 1px solid rgba(135,135,135,200); }"
        "QToolButton:pressed, QToolButton:checked { background-color: rgba(120,48,65,235); border: 1px solid rgba(180,115,130,220); color: white; }"
        "QToolButton:disabled { background-color: rgba(45,45,45,180); border: 1px solid rgba(75,75,75,160); color: rgba(255,255,255,95); }"
        "QComboBox { background-color: rgba(60,60,60,210); border: 1px solid rgba(105,105,105,170); border-radius: 4px; color: white; padding: 5px 22px 5px 10px; margin: 0px; font-size: 12px; }"
        "QComboBox:hover { background-color: rgba(82,82,82,230); border: 1px solid rgba(135,135,135,200); }"
        "QComboBox::drop-down { border: none; background: transparent; width: 20px; }"
        "QComboBox QAbstractItemView { background-color: rgb(42,42,42); border: 1px solid rgba(120,120,120,220); color: white; padding: 4px; outline: 0; selection-background-color: rgba(70,125,220,230); selection-color: white; }");
}
