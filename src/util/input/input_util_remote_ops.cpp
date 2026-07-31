#include "input_util.h"

#include "common/logger_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#endif


bool InputUtil::execRemoteOperation(const QString &action, QString *errorMessage)
{
    const QString normalized = action.trimmed().toLower();

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    if (normalized == QStringLiteral("sas"))
        return InputUtil::sendSecureAttentionSequence(errorMessage);
    if (normalized == QStringLiteral("lock"))
        return LockWorkStation() != FALSE;
    if (normalized == QStringLiteral("logoff"))
        return ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_MAJOR_APPLICATION) != FALSE;
    if (normalized == QStringLiteral("restart"))
        return QProcess::startDetached(QStringLiteral("shutdown"), QStringList() << QStringLiteral("/r") << QStringLiteral("/t") << QStringLiteral("0"));
    if (normalized == QStringLiteral("shutdown"))
        return QProcess::startDetached(QStringLiteral("shutdown"), QStringList() << QStringLiteral("/s") << QStringLiteral("/t") << QStringLiteral("0"));
    if (normalized == QStringLiteral("num_lock"))
    {
        InputUtil::execKeyboardEvent(VK_NUMLOCK, QStringLiteral("down"));
        InputUtil::execKeyboardEvent(VK_NUMLOCK, QStringLiteral("up"));
        return true;
    }
    if (normalized == QStringLiteral("resource_manager"))
        return QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList());
    if (normalized == QStringLiteral("task_manager"))
        return QProcess::startDetached(QStringLiteral("taskmgr.exe"), QStringList());
#elif defined(Q_OS_MACOS)
    if (normalized == QStringLiteral("lock"))
    {
        const QString cgSession = QStringLiteral("/System/Library/CoreServices/Menu Extras/User.menu/Contents/Resources/CGSession");
        if (QFileInfo(cgSession).isExecutable())
            return QProcess::startDetached(cgSession, QStringList() << QStringLiteral("-suspend"));
        return QProcess::startDetached(QStringLiteral("osascript"),
                                       QStringList() << QStringLiteral("-e")
                                                     << QStringLiteral("tell application \"System Events\" to keystroke \"q\" using {control down, command down}"));
    }
    if (normalized == QStringLiteral("logoff"))
        return QProcess::startDetached(QStringLiteral("osascript"), QStringList() << QStringLiteral("-e") << QStringLiteral("tell app \"System Events\" to log out"));
    if (normalized == QStringLiteral("restart"))
        return QProcess::startDetached(QStringLiteral("osascript"), QStringList() << QStringLiteral("-e") << QStringLiteral("tell app \"System Events\" to restart"));
    if (normalized == QStringLiteral("shutdown"))
        return QProcess::startDetached(QStringLiteral("osascript"), QStringList() << QStringLiteral("-e") << QStringLiteral("tell app \"System Events\" to shut down"));
    if (normalized == QStringLiteral("resource_manager"))
        return QProcess::startDetached(QStringLiteral("open"), QStringList() << QStringLiteral("-a") << QStringLiteral("Finder"));
    if (normalized == QStringLiteral("task_manager"))
        return QProcess::startDetached(QStringLiteral("open"), QStringList() << QStringLiteral("-a") << QStringLiteral("Activity Monitor"));
#else
    if (normalized == QStringLiteral("lock"))
        return QProcess::startDetached(QStringLiteral("loginctl"), QStringList() << QStringLiteral("lock-session"));
    if (normalized == QStringLiteral("logoff"))
        return QProcess::startDetached(QStringLiteral("loginctl"), QStringList() << QStringLiteral("terminate-user") << qgetenv("USER"));
    if (normalized == QStringLiteral("restart"))
        return QProcess::startDetached(QStringLiteral("systemctl"), QStringList() << QStringLiteral("reboot"));
    if (normalized == QStringLiteral("shutdown"))
        return QProcess::startDetached(QStringLiteral("systemctl"), QStringList() << QStringLiteral("poweroff"));
    if (normalized == QStringLiteral("resource_manager"))
        return QProcess::startDetached(QStringLiteral("xdg-open"), QStringList() << QDir::homePath());
    if (normalized == QStringLiteral("task_manager"))
        return QProcess::startDetached(QStringLiteral("xterm"), QStringList() << QStringLiteral("-e") << QStringLiteral("top"));
#endif

    if (errorMessage)
        *errorMessage = tr("Unsupported remote operation");
    LOG_WARN("Unsupported remote operation: {}", action);
    return false;
}
