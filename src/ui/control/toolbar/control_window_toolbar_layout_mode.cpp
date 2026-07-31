#include "ui/control/control_window.h"
#include "ui/control/control_window_view_helpers.h"

#include <QBoxLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>


bool ControlWindow::isRemotePortrait() const
{
    return m_remoteResolution.isValid() &&
           m_remoteResolution.height() > m_remoteResolution.width();
}


bool ControlWindow::shouldPlaceToolbarInSidePanel() const
{
    return m_androidNavHost &&
           isRemotePortrait();
}


void ControlWindow::applyToolbarLayoutMode(bool sidePanelMode)
{
    if (!m_floatingToolbar || !m_toolbarButtonLayout || !m_statsLabel)
        return;

    constexpr int sideControlWidth = 124;

    m_toolbarButtonLayout->setDirection(sidePanelMode ? QBoxLayout::TopToBottom
                                                      : QBoxLayout::LeftToRight);
    m_toolbarButtonLayout->setSpacing(sidePanelMode ? 6 : 4);

    if (auto *layout = m_floatingToolbar->layout())
    {
        layout->setContentsMargins(sidePanelMode ? 8 : 24,
                                   sidePanelMode ? 8 : 7,
                                   sidePanelMode ? 8 : 24,
                                   sidePanelMode ? 8 : 8);
        layout->setSpacing(sidePanelMode ? 6 : 5);
    }

    auto applyButtonWidth = [sidePanelMode, sideControlWidth](QPushButton *button) {
        if (!button)
            return;
        if (sidePanelMode)
        {
            button->setFixedWidth(sideControlWidth);
            button->setMinimumHeight(30);
        }
        else
        {
            fitControlButtonWidthToText(button);
            button->setMinimumHeight(0);
        }
    };
    auto applyToolButtonWidth = [sidePanelMode, sideControlWidth](QToolButton *button) {
        if (!button)
            return;
        if (sidePanelMode)
        {
            button->setFixedWidth(sideControlWidth);
            button->setMinimumHeight(38);
        }
        else
        {
            fitControlToolButtonWidthToText(button);
            button->setMinimumHeight(0);
        }
    };
    applyButtonWidth(m_screenshotBtn);
    applyToolButtonWidth(m_switchScreenBtn);
    applyToolButtonWidth(m_remoteOperationBtn);
    applyButtonWidth(m_fileTransferBtn);
    applyToolButtonWidth(m_audioCaptureBtn);
    applyButtonWidth(m_transferRecordBtn);
    for (QToolButton *button : m_sideMenuButtons)
    {
        if (!button)
            continue;
        if (sidePanelMode)
        {
            button->setFixedWidth(sideControlWidth);
            button->setMinimumHeight(38);
        }
        else
        {
            button->setMinimumWidth(0);
            button->setMaximumWidth(QWIDGETSIZE_MAX);
            button->setMinimumHeight(0);
            fitControlToolButtonWidthToText(button);
        }
    }
    if (m_toolbarButtonRow)
    {
        if (sidePanelMode)
        {
            m_toolbarButtonRow->setFixedWidth(sideControlWidth);
        }
        else
        {
            m_toolbarButtonRow->setMinimumWidth(0);
            m_toolbarButtonRow->setMaximumWidth(QWIDGETSIZE_MAX);
            m_toolbarButtonRow->adjustSize();
        }
    }

    m_statsLabel->setWordWrap(sidePanelMode);
    m_statsLabel->setAlignment(sidePanelMode ? (Qt::AlignLeft | Qt::AlignVCenter)
                                             : Qt::AlignCenter);
    if (sidePanelMode)
    {
        m_statsLabel->setMinimumWidth(sideControlWidth);
        m_statsLabel->setMaximumWidth(sideControlWidth);
    }
    else
    {
        QFontMetrics statsFm(m_statsLabel->font());
        const int statsTextW = controlWindowTextWidth(statsFm, m_statsLabel->text());
        const int padding = 24;
        const int minAdaptive = qMax(220, statsTextW + padding);
        m_statsLabel->setMinimumWidth(minAdaptive);
        m_statsLabel->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    m_statsLabel->setStyleSheet(QStringLiteral("QLabel { color: rgba(255,255,255,230); background: transparent; border: none; padding: 0px 2px; font-size: %1px; }")
                                    .arg(sidePanelMode ? 12 : 10));

    if (m_toolbarButtonRow)
        m_toolbarButtonRow->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}
