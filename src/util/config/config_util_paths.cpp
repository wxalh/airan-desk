#include "config_util_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QUuid>
#include <spdlog/spdlog.h>

namespace config_util_internal
{
namespace
{

QString uuidWithoutBraces()
{
    QString text = QUuid::createUuid().toString();
    if (text.startsWith(QLatin1Char('{')) && text.endsWith(QLatin1Char('}')))
        text = text.mid(1, text.size() - 2);
    return text;
}

QString userConfDir()
{
    const QString homeDir = QDir::homePath();
    if (!homeDir.isEmpty())
        return QDir(homeDir).filePath(QStringLiteral(".wxalh/airan-desk/conf"));
    return QString();
}

QString userBaseDir()
{
    const QString homeDir = QDir::homePath();
    if (!homeDir.isEmpty())
        return QDir(homeDir).filePath(QStringLiteral(".wxalh/airan-desk"));
    return QString();
}


QString runtimeConfDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("conf"));
}

QString runtimeBaseDir()
{
    return QCoreApplication::applicationDirPath();
}


bool isDirectoryWritable(const QString &dirPath)
{
    if (dirPath.isEmpty())
        return false;

    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    const QString probePath = dir.filePath(QStringLiteral(".airan-write-test-%1.tmp")
                                               .arg(uuidWithoutBraces()));
    QFile probe(probePath);
    if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    probe.write("ok");
    probe.close();
    probe.remove();
    return true;
}

bool isTargetWritable(const QString &dirPath, const QString &fileName)
{
    if (!isDirectoryWritable(dirPath))
        return false;
    const QString targetPath = QDir(dirPath).filePath(fileName);
    const QFileInfo info(targetPath);
    if (!info.exists())
        return true;
    if (!info.isFile())
        return false;
    QFile target(targetPath);
    return target.open(QIODevice::ReadWrite);
}


QString selectedWritableConfDir()
{
    return QFileInfo(selectWritableConfFile(
                         runtimeConfDir(), userConfDir(), QStringLiteral("common.ini")))
        .absolutePath();
}

} /* namespace */

QString selectWritableConfFile(const QString &runtimeDir,
                               const QString &userDir,
                               const QString &fileName)
{
    if (isTargetWritable(runtimeDir, fileName))
        return QDir(runtimeDir).filePath(fileName);
    if (!userDir.isEmpty())
    {
        if (!isTargetWritable(userDir, fileName))
            SPDLOG_WARN("User config target is not writable: {}",
                        QDir(userDir).filePath(fileName).toStdString());
        return QDir(userDir).filePath(fileName);
    }
    return QDir(runtimeDir).filePath(fileName);
}

QString selectWritableDirectory(const QString &runtimeDir, const QString &userDir)
{
    if (isDirectoryWritable(runtimeDir))
        return runtimeDir;
    if (!userDir.isEmpty())
    {
        if (!isDirectoryWritable(userDir))
            SPDLOG_WARN("User data directory is not writable: {}", userDir.toStdString());
        return userDir;
    }
    return runtimeDir;
}

void configureDefaultSettingsPath()
{
    const QString settingsDir = selectedWritableConfDir();
    if (!settingsDir.isEmpty())
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    QSettings::setDefaultFormat(QSettings::IniFormat);
}

QString writableConfFile(const QString &fileName, bool copyDefault)
{
    const bool isAiranIni = fileName.compare(QStringLiteral("airan.ini"), Qt::CaseInsensitive) == 0;
    QString targetPath;
    if (isAiranIni)
        targetPath = QDir(userConfDir()).filePath(fileName);
    else
        targetPath = selectWritableConfFile(runtimeConfDir(), userConfDir(), fileName);

    QString dirPath = QFileInfo(targetPath).absolutePath();
    if (dirPath.isEmpty())
    {
        SPDLOG_WARN("User config directory is unavailable for {}", fileName.toStdString());
        dirPath = QDir::tempPath();
        targetPath = QDir(dirPath).filePath(fileName);
    }

    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        SPDLOG_WARN("Failed to create config directory: {}", dirPath.toStdString());

    if (QFile::exists(targetPath))
        return targetPath;

    const QString defaultPath = QDir(runtimeConfDir()).filePath(fileName);
    if (copyDefault && defaultPath != targetPath && QFile::exists(defaultPath))
    {
        if (!QFile::copy(defaultPath, targetPath))
        {
            SPDLOG_WARN("Failed to copy default config from {} to {}",
                        defaultPath.toStdString(),
                        targetPath.toStdString());
        }
    }

    return targetPath;
}

QString writableLogDir()
{
    const QString userBase = userBaseDir();
    const QString userLogs = userBase.isEmpty()
                                 ? QString()
                                 : QDir(userBase).filePath(QStringLiteral("logs"));
    return selectWritableDirectory(
        QDir(runtimeBaseDir()).filePath(QStringLiteral("logs")),
        userLogs);
}

} /* namespace config_util_internal */
