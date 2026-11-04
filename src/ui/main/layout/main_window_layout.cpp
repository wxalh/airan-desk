#include "ui/main/main_window.h"

#include "app/app_style.h"
#include "ui/common/adaptive_ui.h"
#include "ui/chrome/app_title_bar.h"
#include "ui/main/layout/main_window_home_layout.h"

void MainWindow::applyMainScale()
{
    // Qt high-DPI scaling already supplies logical widget and screen coordinates.
    m_dpiScale = 1.0;
    m_uiScale = UiAdaptive::scaleFactor();
    if (m_titleBar)
        m_titleBar->setUiScale(m_uiScale);
    if (m_content)
        m_content->setMinimumSize(0, 0);
    UiAdaptive::applyPreferredWindowSize(this, QSize(960, 720), QSize(600, 450));
    layoutMainContent();
}

void MainWindow::layoutMainContent()
{
    const double scale = UiAdaptive::scaleFactor();
    if (m_titleBar && qAbs(m_uiScale - scale) > 0.01)
    {
        m_uiScale = scale;
        m_titleBar->setUiScale(scale);
    }
    if (m_content)
        HomePageLayout::apply(m_content);
}
