#include "ui/main/main_window.h"

#include "ui/main/layout/main_window_layout_metrics.h"
#include "ui/widgets/password/password_line_edit_util.h"

#include <QLineEdit>


void MainWindow::layoutMainTextFields(const MainWindowLayoutMetrics &metrics)
{
    m_localIdBorder->setGeometry(metrics.fieldX, metrics.groupTop + scaled(72), metrics.fieldW, scaled(45));
    m_localPwdBorder->setGeometry(metrics.fieldX, metrics.groupTop + scaled(156), metrics.fieldW, scaled(45));
    m_remoteIdBorder->setGeometry(metrics.fieldX, metrics.groupTop + scaled(324) + metrics.remoteFieldsYOffset, metrics.fieldW, scaled(45));
    m_remotePwdBorder->setGeometry(metrics.fieldX, metrics.groupTop + scaled(408) + metrics.remoteFieldsYOffset, metrics.fieldW, scaled(45));

    m_localIdEdit->setFont(scaledFont(15));
    m_localIdEdit->setGeometry(metrics.fieldX + metrics.inputLeftInset,
                               metrics.groupTop + scaled(81),
                               metrics.actionInputW,
                               scaled(30));

    m_localPwdEdit->setFont(scaledFont(15));
    m_localPwdEdit->setGeometry(metrics.fieldX + metrics.inputLeftInset,
                                metrics.groupTop + scaled(165),
                                metrics.actionInputW,
                                scaled(30));

    m_remoteIdEdit->setFont(scaledFont(15));
    m_remoteIdEdit->setGeometry(metrics.fieldX + metrics.inputLeftInset,
                                metrics.groupTop + scaled(333) + metrics.remoteFieldsYOffset,
                                metrics.inputW,
                                scaled(30));

    const QFont remotePwdFont = scaledFont(15);
    m_remotePwdEdit->setFont(remotePwdFont);
    m_remotePwdEdit->setGeometry(metrics.fieldX + metrics.inputLeftInset,
                                 metrics.groupTop + scaled(417) + metrics.remoteFieldsYOffset,
                                 metrics.actionInputW,
                                 scaled(30));
    setPasswordRevealFonts(m_remotePwdEdit, remotePwdFont, remotePwdFont);
}
