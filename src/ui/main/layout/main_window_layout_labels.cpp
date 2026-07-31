#include "ui/main/main_window.h"

#include "ui/common/adaptive_ui.h"
#include "ui/main/layout/main_window_layout_metrics.h"

#include <QFontMetrics>
#include <QLabel>


void MainWindow::layoutMainSectionLabels(MainWindowLayoutMetrics &metrics)
{
    m_allowControlLabel->setFont(scaledFont(13));
    QFontMetrics fmAllow(m_allowControlLabel->font());
    const int allowW = qMax(scaled(131), UiAdaptive::textWidth(fmAllow, m_allowControlLabel->text()) + scaled(20));
    m_allowControlLabel->setGeometry(metrics.sectionLabelX, metrics.groupTop, allowW, scaled(31));

    m_remoteControlLabel->setFont(scaledFont(13));
    QFontMetrics fmRemoteControl(m_remoteControlLabel->font());
    metrics.remoteControlW = qMax(scaled(131), UiAdaptive::textWidth(fmRemoteControl, m_remoteControlLabel->text()) + scaled(20));
    m_remoteControlLabel->setGeometry(metrics.sectionLabelX, metrics.groupTop + scaled(252), metrics.remoteControlW, scaled(31));

    const QFont smallFont = scaledFont(11);
    QFontMetrics fmSmall(smallFont);

    m_localIdLabel->setFont(smallFont);
    const int localIdW = qMax(scaled(76), UiAdaptive::textWidth(fmSmall, m_localIdLabel->text()) + scaled(10));
    m_localIdLabel->setGeometry(metrics.baseLabelX, metrics.groupTop + scaled(44), localIdW, scaled(18));

    m_localPwdLabel->setFont(smallFont);
    const int localPwdW = qMax(scaled(76), UiAdaptive::textWidth(fmSmall, m_localPwdLabel->text()) + scaled(10));
    m_localPwdLabel->setGeometry(metrics.baseLabelX, metrics.groupTop + scaled(128), localPwdW, scaled(18));

    m_remoteIdLabel->setFont(smallFont);
    const int remoteIdW = qMax(scaled(76), UiAdaptive::textWidth(fmSmall, m_remoteIdLabel->text()) + scaled(10));
    m_remoteIdLabel->setGeometry(metrics.baseLabelX, metrics.groupTop + scaled(296) + metrics.remoteFieldsYOffset, remoteIdW, scaled(18));

    m_remotePwdLabel->setFont(smallFont);
    const int remotePwdW = qMax(scaled(76), UiAdaptive::textWidth(fmSmall, m_remotePwdLabel->text()) + scaled(10));
    m_remotePwdLabel->setGeometry(metrics.baseLabelX, metrics.groupTop + scaled(380) + metrics.remoteFieldsYOffset, remotePwdW, scaled(18));

    m_wsConnectStatus->setGeometry(scaled(20),
                                   (m_content ? m_content->height() - scaled(30) : scaled(570)),
                                   scaled(281),
                                   scaled(18));
    if (m_versionLabel)
    {
        m_versionLabel->setFont(scaledFont(9));
        QFontMetrics fmVersion(m_versionLabel->font());
        const int versionW = qMax(scaled(120), UiAdaptive::textWidth(fmVersion, m_versionLabel->text()) + scaled(12));
        m_versionLabel->setGeometry(metrics.contentW - versionW - scaled(20),
                                    (m_content ? m_content->height() - scaled(30) : scaled(570)),
                                    versionW,
                                    scaled(18));
    }
}
