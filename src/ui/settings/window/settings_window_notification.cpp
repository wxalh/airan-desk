#include "settings_window.h"

#include "security/notification_script_runner.h"
#include "util/config/config_util.h"

#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

void SettingsWindow::chooseNotificationScript()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select notification script"),
        m_notificationScriptEdit->text().trimmed(),
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
        tr("Scripts (*.bat *.cmd *.ps1);;All files (*)")
#else
        tr("Scripts (*.sh);;All files (*)")
#endif
    );
    if (!path.isEmpty())
        m_notificationScriptEdit->setText(path);
}

void SettingsWindow::testNotificationScript()
{
    const QString path = m_notificationScriptEdit->text().trimmed();
    if (path.isEmpty())
    {
        QMessageBox::warning(this, tr("Notification script"), tr("Select a notification script first."));
        return;
    }
    m_notificationScriptTestButton->setEnabled(false);
    NotificationScriptRunner::runAsync(
        this,
        QStringLiteral("test"),
        QStringLiteral("local"),
        QStringLiteral("notification script test"),
        [this](const NotificationScriptResult &result) {
            if (m_notificationScriptTestButton)
                m_notificationScriptTestButton->setEnabled(true);
            if (result.success)
                QMessageBox::information(this, tr("Notification script"), tr("Notification script test succeeded."));
            else
                QMessageBox::critical(this, tr("Notification script"), result.message);
        },
        10000,
        path);
}
