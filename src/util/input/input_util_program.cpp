#include "input_util.h"

#include "common/logger_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif


bool InputUtil::runProgram(const QString &path, QString *errorMessage)
{
    QFileInfo info(path);
    if (!info.exists() || !info.isFile())
    {
        if (errorMessage)
            *errorMessage = tr("File does not exist or is not a regular file");
        LOG_WARN("runProgram rejected invalid path: {}", path);
        return false;
    }

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    const QString nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
    const QString nativeDir = QDir::toNativeSeparators(info.absolutePath());
    HINSTANCE result = ShellExecuteW(nullptr,
                                     L"open",
                                     reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                     nullptr,
                                     reinterpret_cast<LPCWSTR>(nativeDir.utf16()),
                                     SW_SHOWNORMAL);
    const bool ok = reinterpret_cast<INT_PTR>(result) > 32;
    if (!ok && errorMessage)
            *errorMessage = tr("ShellExecute failed");
    LOG_DEBUG("runProgram {} path={}", ok ? "succeeded" : "failed", info.absoluteFilePath());
    return ok;
#else
    const bool ok = QProcess::startDetached(info.absoluteFilePath(), QStringList(), info.absolutePath());
    if (!ok && errorMessage)
        *errorMessage = tr("Failed to start program");
    LOG_DEBUG("runProgram {} path={}", ok ? "succeeded" : "failed", info.absoluteFilePath());
    return ok;
#endif
}
