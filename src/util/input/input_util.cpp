#include "input_util.h"


InputUtil::InputUtil(QObject *parent)
    : QObject{parent}
{
}

#if !defined(Q_OS_WIN64) && !defined(Q_OS_WIN32)
bool InputUtil::prepareWindowsInputBroker(QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    return true;
}

QString InputUtil::windowsInputBrokerServerName()
{
    return QString();
}

bool InputUtil::authenticateWindowsInputBrokerRequest(QJsonObject *request)
{
    Q_UNUSED(request);
    return false;
}

bool InputUtil::isWindowsUnattendedInputInstalled()
{
    return false;
}

bool InputUtil::isWindowsUnattendedInputUpdateRequired(QString *reason)
{
    Q_UNUSED(reason);
    return false;
}

bool InputUtil::ensureWindowsUnattendedInputServiceReady(QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    return false;
}

bool InputUtil::installWindowsUnattendedInput(QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    return false;
}

bool InputUtil::uninstallWindowsUnattendedInput(QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    return false;
}

bool InputUtil::stopWindowsUnattendedInputService(QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    return false;
}

void InputUtil::setWindowsSessionLocked(bool locked)
{
    Q_UNUSED(locked);
}

bool InputUtil::isWindowsSessionLocked()
{
    return false;
}

void InputUtil::shutdownWindowsInputBroker()
{
}

void InputUtil::cancelWindowsUnattendedInputReadiness()
{
}

void InputUtil::resetWindowsUnattendedInputReadinessCancellation()
{
}

int InputUtil::runWindowsInputBroker(int argc, char *argv[])
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return 1;
}

int InputUtil::runWindowsInputBrokerService(int argc, char *argv[])
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return 1;
}

int InputUtil::runWindowsServiceElevatedCommand(int argc, char *argv[])
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return 1;
}

bool InputUtil::sendSecureAttentionSequence(QString *errorMessage)
{
    if (errorMessage)
        *errorMessage = tr("Secure attention sequence is only supported on Windows");
    return false;
}
#endif
