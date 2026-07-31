#include "ui/main/main_window.h"

#include "ui/common/adaptive_ui.h"
#include "ui/chrome/app_title_bar.h"
#include "ui/main/layout/main_window_layout_metrics.h"


void MainWindow::applyMainScale()
{
    constexpr int kBaseWindowWidth = 800;
    constexpr int kBaseWindowHeight = 638;
    constexpr int kBaseContentWidth = 800;
    constexpr int kBaseContentHeight = 600;

    if (QScreen *screen = UiAdaptive::screenForWidget(this))
        m_dpiScale = qBound(0.75, screen->logicalDotsPerInch() / 96.0, 3.0);
    else
        m_dpiScale = 1.0;

    m_uiScale = UiAdaptive::proportionalScale(this, QSize(kBaseWindowWidth, kBaseWindowHeight), 0.72, 1.0, 0.45);

    if (m_titleBar)
        m_titleBar->setUiScale(m_uiScale * m_dpiScale);
    if (m_content)
        m_content->setMinimumSize(UiAdaptive::scaledSize(QSize(kBaseContentWidth, kBaseContentHeight), 0.45 * m_dpiScale));
    setMinimumSize(UiAdaptive::scaledSize(QSize(kBaseWindowWidth, kBaseWindowHeight), 0.45 * m_dpiScale));
    resize(scaled(kBaseWindowWidth), scaled(kBaseWindowHeight));
    layoutMainContent();
}


void MainWindow::layoutMainContent()
{
    constexpr int kBaseContentWidth = 800;
    constexpr int kBaseContentHeight = 600;

    QSize contentSize = m_content ? m_content->size() : QSize();
    if (m_content)
    {
        const int contentTop = qMax(0, m_content->y());
        const QSize windowContentSize(qMax(0, width()),
                                      qMax(0, height() - contentTop));
        contentSize = contentSize.expandedTo(windowContentSize);
    }

    if (!contentSize.isEmpty())
    {
        if (QScreen *screen = UiAdaptive::screenForWidget(this))
            m_dpiScale = qBound(0.75, screen->logicalDotsPerInch() / 96.0, 3.0);
        else
            m_dpiScale = 1.0;

        m_uiScale = qMin(contentSize.width() / (static_cast<double>(kBaseContentWidth) * m_dpiScale),
                         contentSize.height() / (static_cast<double>(kBaseContentHeight) * m_dpiScale));
        m_uiScale = qBound(0.45, m_uiScale, 1.0);
        if (m_titleBar)
            m_titleBar->setUiScale(m_uiScale * m_dpiScale);
    }

    MainWindowLayoutMetrics metrics;
    styleMainActionButtons(metrics);
    metrics = calculateMainLayoutMetrics();
    layoutMainSectionLabels(metrics);
    layoutMainModeRadios(metrics);
    layoutMainSectionLabels(metrics);
    layoutMainTextFields(metrics);
    layoutMainActionButtons(metrics);
}
