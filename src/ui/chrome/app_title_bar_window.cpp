#include "app_title_bar.h"
#include "app_title_bar_icons.h"

#include <QPushButton>


void AppTitleBar::toggleMaximize()
{
    if (!m_targetWindow)
        return;

    if (m_targetWindow->isMaximized())
        m_targetWindow->showNormal();
    else
        m_targetWindow->showMaximized();
    updateMaximizeButton();
}


void AppTitleBar::updateMaximizeButton()
{
    if (!m_targetWindow || !m_maximizeButton)
        return;

    m_maximizeButton->setIcon(makeTitleBarIcon(m_targetWindow->isMaximized()
                                                   ? TitleBarGlyph::Restore
                                                   : TitleBarGlyph::Maximize));
    m_maximizeButton->setToolTip(m_targetWindow->isMaximized() ? tr("Restore") : tr("Maximize"));
}
