#include "ui/main/main_window.h"

#include "ui/main/layout/main_window_layout_metrics.h"

#include <QPushButton>


void MainWindow::styleMainActionButtons(const MainWindowLayoutMetrics &metrics)
{
    Q_UNUSED(metrics);
    const QFont buttonFont = scaledFont(13);
    for (auto *button : {m_connectButton, m_localPwdChangeButton, m_localShareButton})
    {
        button->setFont(buttonFont);
        button->setStyleSheet(QStringLiteral(
                                  "QPushButton {"
                                  "    font-size: %1pt;"
                                  "    background-color: rgba(120, 48, 65, 92);"
                                  "    border: 0;"
                                  "    border-top-right-radius: %2px;"
                                  "    border-bottom-right-radius: %2px;"
                                  "    border-top-left-radius: 0px;"
                                  "    border-bottom-left-radius: 0px;"
                                  "    color: rgb(131,193,224);"
                                  "    padding: 0px 7px;"
                                  "}"
                                  "QPushButton:hover {"
                                  "    background-color: rgba(120, 48, 65, 150);"
                                  "    color: #d8f3ff;"
                                  "}"
                                  "QPushButton:pressed {"
                                  "    background-color: rgba(93, 38, 52, 170);"
                                  "    color: #ffffff;"
                                  "}")
                                  .arg(buttonFont.pointSizeF(), 0, 'f', 1)
                                  .arg(scaled(8)));
    }
}


void MainWindow::layoutMainActionButtons(const MainWindowLayoutMetrics &metrics)
{
    m_connectButton->setGeometry(metrics.actionX,
                                 metrics.groupTop + scaled(408) + metrics.remoteFieldsYOffset + metrics.actionYInset,
                                 metrics.buttonColumnW,
                                 metrics.actionH);
    m_localPwdChangeButton->setGeometry(metrics.actionX,
                                        metrics.groupTop + scaled(156) + metrics.actionYInset,
                                        metrics.buttonColumnW,
                                        metrics.actionH);
    m_localShareButton->setGeometry(metrics.actionX,
                                    metrics.groupTop + scaled(72) + metrics.actionYInset,
                                    metrics.buttonColumnW,
                                    metrics.actionH);

    m_connectDivider->setGeometry(metrics.actionX,
                                  metrics.groupTop + scaled(408) + metrics.remoteFieldsYOffset + metrics.dividerYInset,
                                  metrics.dividerW,
                                  metrics.dividerH);
    m_localPwdChangeDivider->setGeometry(metrics.actionX,
                                         metrics.groupTop + scaled(156) + metrics.dividerYInset,
                                         metrics.dividerW,
                                         metrics.dividerH);
    m_localShareDivider->setGeometry(metrics.actionX,
                                     metrics.groupTop + scaled(72) + metrics.dividerYInset,
                                     metrics.dividerW,
                                     metrics.dividerH);

    m_connectDivider->raise();
    m_localPwdChangeDivider->raise();
    m_localShareDivider->raise();
    m_connectButton->raise();
    m_localPwdChangeButton->raise();
    m_localShareButton->raise();

    m_settingsButton->setGeometry(metrics.contentW - scaled(44), scaled(12), scaled(36), scaled(36));
    m_settingsButton->setIconSize(QSize(scaled(20), scaled(20)));
}
