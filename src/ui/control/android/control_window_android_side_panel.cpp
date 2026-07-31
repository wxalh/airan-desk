#include "ui/control/control_window.h"


void ControlWindow::updateAndroidSidePanelWidth()
{
    if (!m_androidNavHost)
        return;

    const bool sidePanelMode = isRemotePortrait();
    const bool hostVisible = m_androidNavigationVisible || sidePanelMode;

    m_androidNavHost->setVisible(hostVisible);
    if (m_androidNavPanel)
        m_androidNavPanel->setVisible(m_androidNavigationVisible);

    if (!hostVisible)
    {
        m_androidNavHost->setFixedWidth(0);
        return;
    }

    applyToolbarLayoutMode(sidePanelMode);

    int width = 152;
    if (sidePanelMode && m_floatingToolbar)
    {
        m_floatingToolbar->adjustSize();
        width = qMax(width, m_floatingToolbar->width() + 16);
    }

    m_androidNavHost->setFixedWidth(width);
    m_androidNavHost->updateGeometry();
}
