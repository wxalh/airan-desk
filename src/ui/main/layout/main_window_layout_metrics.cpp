#include "ui/main/main_window.h"

#include "ui/common/adaptive_ui.h"
#include "ui/main/layout/main_window_layout_metrics.h"

#include <QFontMetrics>
#include <QPushButton>


MainWindowLayoutMetrics MainWindow::calculateMainLayoutMetrics()
{
    constexpr int kBaseContentWidth = 800;
    constexpr int kBaseContentHeight = 600;

    MainWindowLayoutMetrics metrics;
    metrics.contentW = m_content ? m_content->width() : scaled(kBaseContentWidth);
    metrics.contentH = m_content ? m_content->height() : scaled(kBaseContentHeight);
    metrics.groupW = qMax(scaled(300), qMin(metrics.contentW - scaled(96), scaled(618)));

    const int groupHeight = scaled(453);
    metrics.groupX = qMax(scaled(24), (metrics.contentW - metrics.groupW) / 2);
    const int contentTop = m_content ? m_content->y() : 0;
    const int windowH = height() > 0 ? height() : metrics.contentH + contentTop;
    metrics.groupTop = qMax(scaled(18), (windowH - groupHeight) / 2 - contentTop);

    auto buttonWidth = [this](QPushButton *button, int baseWidth) {
        QFontMetrics fm(button->font());
        return qMax(scaled(baseWidth), UiAdaptive::textWidth(fm, button->text()) + scaled(16));
    };

    const int shareButtonW = buttonWidth(m_localShareButton, 54);
    const int updateButtonW = buttonWidth(m_localPwdChangeButton, 54);
    const int connectButtonW = buttonWidth(m_connectButton, 62);
    metrics.buttonColumnW = qMax(connectButtonW, qMax(shareButtonW, updateButtonW));
    metrics.fieldX = metrics.groupX;
    metrics.fieldW = metrics.groupW;
    metrics.inputLeftInset = scaled(8);
    metrics.inputRightInset = scaled(1);
    metrics.actionGap = scaled(10);
    metrics.actionH = scaled(45);
    metrics.actionYInset = scaled(1);
    metrics.dividerW = qMax(1, scaled(1));
    metrics.dividerH = qMax(1, metrics.actionH * 2 / 3);
    metrics.dividerYInset = metrics.actionYInset + (metrics.actionH - metrics.dividerH) / 2;
    metrics.inputW = metrics.fieldW - scaled(16);
    metrics.baseLabelX = metrics.fieldX;
    metrics.sectionLabelX = metrics.fieldX;
    metrics.actionX = metrics.fieldX + metrics.fieldW - metrics.inputRightInset - metrics.buttonColumnW;
    metrics.actionInputW = qMax(scaled(180), metrics.actionX - metrics.actionGap - (metrics.fieldX + metrics.inputLeftInset));
    return metrics;
}
