#include "settings_window.h"

#include "ui/common/message_box_util.h"
#include "util/config/config_util.h"
#include "util/input/input_util.h"
#include "security/controlled_access_gate.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

void SettingsWindow::refreshServiceState()
{
    if (!m_serviceRepairButton || !m_serviceStatusLabel)
        return;

    const bool installed = isServiceInstalled();
    m_serviceStatusLabel->setText(installed ? tr("Airan Desk service is installed.")
                                            : tr("Airan Desk service is not installed."));
}

void SettingsWindow::repairWindowsService()
{
    startServiceOperation();
}


void SettingsWindow::startServiceOperation()
{
    if (m_serviceOperationRunning)
        return;

    m_serviceOperationRunning = true;
    if (ConfigUtil->allow_remote)
        ControlledAccessGate::setRuntimePrerequisiteReady(false);
    if (m_serviceRepairButton)
        m_serviceRepairButton->setEnabled(false);
    if (m_serviceStatusLabel)
        m_serviceStatusLabel->setText(tr("Repairing Airan Desk service..."));
    const bool allowRemote = ConfigUtil->allow_remote;
    const std::shared_ptr<std::atomic_bool> callbackState = m_asyncCallbacksAlive;
    SettingsWindow *const receiver = this;
    m_serviceOperationThread = std::thread([callbackState, receiver, allowRemote]() {
        QString errorMessage;
        const bool status = allowRemote
                                ? InputUtil::installWindowsUnattendedInput(&errorMessage)
                                : InputUtil::uninstallWindowsUnattendedInput(&errorMessage);
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;
        QTimer::singleShot(0, application, [callbackState, receiver, status, errorMessage]() {
            if (callbackState->load())
                receiver->onServiceOperationFinished(status, errorMessage);
        });
    });
    m_serviceOperationThread.detach();
}


void SettingsWindow::onServiceOperationFinished(bool status,
                                                const QString &errorMessage)
{
    m_serviceOperationRunning = false;
    ControlledAccessGate::setRuntimePrerequisiteReady(status && ConfigUtil->allow_remote);
    if (m_serviceRepairButton)
        m_serviceRepairButton->setEnabled(true);

    refreshServiceState();
    if (!status && !m_closeAfterServiceOperation)
    {
        UiMessageBox::critical(this,
                               tr("Service"),
                               errorMessage.isEmpty() ? tr("Failed to repair Airan Desk service.") : errorMessage);
    }
    if (m_closeAfterServiceOperation)
    {
        m_closeAfterServiceOperation = false;
        close();
    }
}

bool SettingsWindow::isServiceInstalled() const
{
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    return InputUtil::isWindowsUnattendedInputInstalled();
#else
    return false;
#endif
}
