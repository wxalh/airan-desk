#include "ui/control/control_window.h"
#include "ui/control/control_window_view_helpers.h"
#include "ui/common/adaptive_ui.h"

#include <QBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QToolButton>

bool ControlWindow::isRemotePortrait() const
{
    return m_remoteResolution.isValid() &&
           m_remoteResolution.height() > m_remoteResolution.width();
}

bool ControlWindow::shouldPlaceToolbarInSidePanel() const
{
    return m_androidNavHost && isRemotePortrait() && width() >= UiAdaptive::scaled(520);
}

void ControlWindow::applyToolbarLayoutMode(bool sidePanelMode)
{
    if (!m_floatingToolbar || !m_toolbarButtonLayout || !m_statsLabel)
        return;

    const int sideControlWidth = UiAdaptive::scaled(124);
    m_floatingToolbar->setProperty("sidePanelMode", sidePanelMode);
    m_toolbarButtonLayout->setDirection(sidePanelMode ? QBoxLayout::TopToBottom
                                                      : QBoxLayout::LeftToRight);
    m_toolbarButtonLayout->setSpacing(UiAdaptive::scaled(sidePanelMode ? 6 : 4));
    if (auto *layout = m_floatingToolbar->layout())
    {
        const int margin = UiAdaptive::scaled(8);
        layout->setContentsMargins(margin, margin, margin, margin);
        layout->setSpacing(UiAdaptive::scaled(6));
    }

    auto applyButtonWidth = [sidePanelMode, sideControlWidth](QPushButton *button) {
        if (!button)
            return;
        fitControlButtonWidthToText(button);
        if (sidePanelMode)
            button->setFixedWidth(qMax(sideControlWidth, button->width()));
        button->setMinimumHeight(UiAdaptive::scaled(30));
    };
    auto applyToolButtonWidth = [sidePanelMode, sideControlWidth](QToolButton *button) {
        if (!button)
            return;
        fitControlToolButtonWidthToText(button);
        if (sidePanelMode)
            button->setFixedWidth(qMax(sideControlWidth, button->width()));
        button->setMinimumHeight(UiAdaptive::scaled(30));
    };
    applyButtonWidth(m_screenshotBtn);
    applyToolButtonWidth(m_switchScreenBtn);
    applyToolButtonWidth(m_remoteOperationBtn);
    applyButtonWidth(m_fileTransferBtn);
    applyToolButtonWidth(m_audioCaptureBtn);
    applyButtonWidth(m_transferRecordBtn);
    for (QToolButton *button : m_sideMenuButtons)
        applyToolButtonWidth(button);

    if (m_toolbarButtonRow)
    {
        m_toolbarButtonRow->setMinimumSize(0, 0);
        m_toolbarButtonRow->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        m_toolbarButtonLayout->activate();
        m_toolbarButtonRow->adjustSize();
    }
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setAlignment(sidePanelMode ? (Qt::AlignLeft | Qt::AlignVCenter)
                                             : Qt::AlignCenter);
    m_statsLabel->setMinimumWidth(0);
    m_statsLabel->setMaximumWidth(QWIDGETSIZE_MAX);
    if (m_toolbarScrollArea)
    {
        m_toolbarScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_toolbarScrollArea->setVerticalScrollBarPolicy(sidePanelMode ? Qt::ScrollBarAsNeeded
                                                                    : Qt::ScrollBarAlwaysOff);
    }
}
