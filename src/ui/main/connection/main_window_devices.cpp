#include "ui/main/main_window.h"
#include "ui/devices/device_manager_dialog.h"
#include "security/runtime_environment.h"

#include <QLineEdit>

void MainWindow::openMyDevices()
{
    if (!RuntimeEnvironment::uiAvailable() || !m_remoteIdEdit || !m_remotePwdEdit)
        return;
    DeviceManagerDialog dialog(this);
    dialog.setInitialDevice(m_remoteIdEdit->text(), m_remotePwdEdit->text());
    connect(&dialog, &DeviceManagerDialog::deviceSelected, this,
            [this](const QString &id, const QString &password) {
        m_remoteIdEdit->setText(id);
        m_remotePwdEdit->setText(password);
        m_remotePwdEdit->setFocus();
    });
    if (m_ws)
    {
        connect(&dialog, &DeviceManagerDialog::queryPresence,
                m_ws, &WsCli::queryDevicePresence, Qt::QueuedConnection);
        connect(m_ws, &WsCli::devicePresenceChanged,
                &dialog, &DeviceManagerDialog::updatePresence, Qt::QueuedConnection);
        connect(m_ws, &WsCli::onWsCliConnected,
                &dialog, &DeviceManagerDialog::refreshPresence, Qt::QueuedConnection);
    }
    if (dialog.openVault())
        dialog.exec();
}
