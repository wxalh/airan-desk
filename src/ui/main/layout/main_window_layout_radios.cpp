#include "ui/main/main_window.h"

#include "ui/common/adaptive_ui.h"
#include "ui/main/layout/main_window_layout_metrics.h"

#include <QRadioButton>


void MainWindow::layoutMainModeRadios(MainWindowLayoutMetrics &metrics)
{
    const int radioGap = scaled(12);
    int radioY = metrics.groupTop + scaled(259);
    int radioX = metrics.sectionLabelX + metrics.remoteControlW + scaled(24);
    for (auto *radio : {m_remoteDesktopRadio, m_remoteFileRadio, m_remoteTerminalRadio})
        radio->setFont(scaledFont(11));

    auto radioWidth = [](QRadioButton *radio) {
        return UiAdaptive::textWidth(radio->fontMetrics(), radio->text()) + 28;
    };
    const int desktopRadioW = qMax(scaled(95), radioWidth(m_remoteDesktopRadio));
    const int fileRadioW = qMax(scaled(95), radioWidth(m_remoteFileRadio));
    const int terminalRadioW = qMax(scaled(95), radioWidth(m_remoteTerminalRadio));
    const int totalRadioW = desktopRadioW + fileRadioW + terminalRadioW + radioGap * 2;
    if (radioX + totalRadioW > metrics.groupX + metrics.groupW)
    {
        radioX = metrics.baseLabelX;
        radioY = metrics.groupTop + scaled(284);
    }

    const int radioBottom = radioY + scaled(22);
    const int fixedRemoteIdLabelY = metrics.groupTop + scaled(296);
    const int minRemoteIdLabelY = radioBottom + scaled(11);
    metrics.remoteFieldsYOffset = qMax(0, minRemoteIdLabelY - fixedRemoteIdLabelY);

    m_remoteDesktopRadio->setGeometry(radioX, radioY, desktopRadioW, scaled(22));
    radioX += desktopRadioW + radioGap;
    m_remoteFileRadio->setGeometry(radioX, radioY, fileRadioW, scaled(22));
    radioX += fileRadioW + radioGap;
    m_remoteTerminalRadio->setGeometry(radioX, radioY, terminalRadioW, scaled(22));
}
