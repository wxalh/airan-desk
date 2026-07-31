#include "license_catalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace LicenseCatalog
{
namespace
{
bool isLicenseDocument(const QString &fileName)
{
    const QString lower = fileName.toLower();
    return lower.contains(QStringLiteral("license")) ||
           lower.contains(QStringLiteral("notice")) ||
           lower.contains(QStringLiteral("copying")) ||
           lower.contains(QStringLiteral("copyright")) ||
           lower.contains(QStringLiteral("patent"));
}
}

QStringList defaultDirectories()
{
    QStringList directories{
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("licenses"))};
#if defined(AIRAN_DESK_INSTALL_DATADIR)
    directories.append(
        QDir(QStringLiteral(AIRAN_DESK_INSTALL_DATADIR)).filePath(QStringLiteral("licenses")));
#endif
    directories.append(
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../share/airan-desk/licenses")));
    return directories;
}

QList<Entry> discover(const QStringList &directories)
{
    QList<Entry> entries;
    QSet<QString> seenNames;
    for (const QString &directoryPath : directories)
    {
        const QDir directory(directoryPath);
        const QFileInfoList files = directory.entryInfoList(
            QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &file : files)
        {
            if (!isLicenseDocument(file.fileName()))
                continue;
            const QString key = file.fileName().toLower();
            if (seenNames.contains(key))
                continue;
            seenNames.insert(key);
            entries.append({file.fileName(), file.absoluteFilePath()});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
        return left.displayName.compare(right.displayName, Qt::CaseInsensitive) < 0;
    });
    return entries;
}

QString readText(const Entry &entry, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    QFile file(entry.filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}
}
