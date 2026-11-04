#include "ui/control/control_window.h"
#include "ui/common/adaptive_ui.h"
#include "ui/control/control_window_view_helpers.h"

#include <QLayout>

void ControlWindow::updateAndroidSidePanelWidth()
{
    if (!m_androidNavHost)
        return;

    const bool sidePanelMode = shouldPlaceToolbarInSidePanel();
    const bool ready = isReceivedImg && !isClosing();
    const bool hostVisible = ready && (m_androidNavigationVisible || sidePanelMode);

    m_androidNavHost->setVisible(hostVisible);
    if (m_androidNavPanel)
        m_androidNavPanel->setVisible(ready && m_androidNavigationVisible);

    if (!hostVisible)
    {
        m_androidNavHost->setFixedWidth(0);
        return;
    }

    applyToolbarLayoutMode(sidePanelMode);

    int panelWidth = UiAdaptive::scaled(sidePanelMode ? 180 : 152);
    if (m_androidNavPanel)
    {
        for (QPushButton *button : {m_androidBackBtn, m_androidHomeBtn, m_androidRecentsBtn})
        {
            fitControlButtonWidthToText(button);
            if (button)
                button->setMinimumHeight(UiAdaptive::scaled(30));
        }
        if (auto *layout = m_androidNavPanel->layout())
        {
            const int margin = UiAdaptive::scaled(10);
            layout->setContentsMargins(margin, margin, margin, margin);
            layout->setSpacing(UiAdaptive::scaled(6));
        }
        panelWidth = qMax(panelWidth, m_androidNavPanel->sizeHint().width() + UiAdaptive::scaled(16));
    }
    panelWidth = qMin(panelWidth, qMax(1, width() / 2));
    m_androidNavHost->setFixedWidth(panelWidth);
    m_androidNavHost->updateGeometry();
}
