#include "input_util.h"

#include "common/logger_manager.h"

#include <QProcess>


bool InputUtil::execAndroidNavigation(const QString &action, QString *errorMessage)
{
    const QString normalized = action.trimmed().toLower();
    QString keyCode;
    if (normalized == QStringLiteral("back"))
        keyCode = QStringLiteral("KEYCODE_BACK");
    else if (normalized == QStringLiteral("home"))
        keyCode = QStringLiteral("KEYCODE_HOME");
    else if (normalized == QStringLiteral("menu"))
        keyCode = QStringLiteral("KEYCODE_MENU");
    else if (normalized == QStringLiteral("recents"))
        keyCode = QStringLiteral("KEYCODE_APP_SWITCH");

    if (keyCode.isEmpty())
    {
        if (errorMessage)
            *errorMessage = tr("Unsupported Android navigation action");
        LOG_WARN("Unsupported Android navigation action: {}", action);
        return false;
    }

#if defined(Q_OS_ANDROID)
    const bool ok = QProcess::execute(QStringLiteral("input"), QStringList() << QStringLiteral("keyevent") << keyCode) == 0;
    if (!ok && errorMessage)
        *errorMessage = tr("Android input keyevent failed");
    LOG_INFO("Android navigation {} action={}", ok ? "succeeded" : "failed", normalized);
    return ok;
#else
    if (errorMessage)
        *errorMessage = tr("Current remote device is not Android");
    LOG_WARN("Android navigation ignored on non-Android platform: {}", action);
    return false;
#endif
}
