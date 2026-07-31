#include "ui/control/control_window.h"
#include "ui/control/control_window_view_helpers.h"

#include <QActionGroup>
#include <QMenu>
#include <QToolButton>

/*
 * Creates the audio, network path, and display mode menus.
 */
void ControlWindow::createToolbarModeMenus()
{
    QMenu *audioMenu = new QMenu(tr("Audio"), m_floatingToolbar);
    m_audioActionGroup = new QActionGroup(this);
    m_audioActionGroup->setExclusive(true);
    addToolbarCheckedAction(audioMenu, m_audioActionGroup, tr("Off"), "off", true);
    addToolbarCheckedAction(audioMenu, m_audioActionGroup, tr("Listen"), "listen");
    addToolbarCheckedAction(audioMenu, m_audioActionGroup, tr("Call"), "call");
    connect(m_audioActionGroup, &QActionGroup::triggered, this, &ControlWindow::onAudioModeActionTriggered);

    m_audioCaptureBtn = new QToolButton(m_floatingToolbar);
    m_audioCaptureBtn->setText(tr("Audio"));
    m_audioCaptureBtn->setToolTip(tr("Select remote audio mode"));
    m_audioCaptureBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_audioCaptureBtn->setPopupMode(QToolButton::InstantPopup);
    m_audioCaptureBtn->setMenu(audioMenu);
    fitControlToolButtonWidthToText(m_audioCaptureBtn);
    m_toolbarButtonLayout->addWidget(m_audioCaptureBtn);

    QMenu *networkMenu = new QMenu(tr("Network path"), m_floatingToolbar);
    m_networkActionGroup = new QActionGroup(this);
    m_networkActionGroup->setExclusive(true);
    addToolbarCheckedAction(networkMenu, m_networkActionGroup, tr("Auto"), "auto", true);
    addToolbarCheckedAction(networkMenu, m_networkActionGroup, tr("Direct"), "direct");
    addToolbarCheckedAction(networkMenu, m_networkActionGroup, tr("UDP relay"), "turn_udp");
    addToolbarCheckedAction(networkMenu, m_networkActionGroup, tr("TCP relay"), "turn_tcp");
    connect(m_networkActionGroup, &QActionGroup::triggered, this, &ControlWindow::onNetworkPathSelected);

    QMenu *displayMenu = new QMenu(tr("Display"), m_floatingToolbar);
    m_displayActionGroup = new QActionGroup(this);
    m_displayActionGroup->setExclusive(true);
    addToolbarCheckedAction(displayMenu, m_displayActionGroup, "1:1", "actual");
    addToolbarCheckedAction(displayMenu, m_displayActionGroup, tr("Fit to window"), "fit", true);
    connect(m_displayActionGroup, &QActionGroup::triggered, this, &ControlWindow::onDisplayModeSelected);

    addToolbarMenuButton(tr("Network path"), networkMenu, m_networkActionGroup);
    addToolbarMenuButton(tr("Display"), displayMenu, m_displayActionGroup);

    m_fitToWindow = true;
    onDisplayModeSelected(m_displayActionGroup->checkedAction());
}
