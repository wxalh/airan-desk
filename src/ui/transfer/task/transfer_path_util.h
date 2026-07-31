#ifndef TRANSFER_PATH_UTIL_H
#define TRANSFER_PATH_UTIL_H

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QUuid>

inline QString transferPathReservationKey(const QString &path, bool caseInsensitive)
{
    QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (caseInsensitive)
        key = key.toCaseFolded();
    return key;
}

inline bool reserveTransferTarget(const QString &path, QSet<QString> *reservedPaths, bool caseInsensitive)
{
    if (!reservedPaths || path.isEmpty())
        return false;
    const QString key = transferPathReservationKey(path, caseInsensitive);
    if (reservedPaths->contains(key))
        return false;
    reservedPaths->insert(key);
    return true;
}

inline QString reserveUniqueDownloadTarget(const QString &baseDir,
                                           const QString &fileName,
                                           QSet<QString> *reservedPaths)
{
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32) || defined(Q_OS_MACOS)
    constexpr bool kCaseInsensitiveLocalPaths = true;
#else
    constexpr bool kCaseInsensitiveLocalPaths = false;
#endif

    const int dotIndex = fileName.lastIndexOf(QLatin1Char('.'));
    const bool hasSuffix = dotIndex > 0 && dotIndex + 1 < fileName.size();
    const QString stem = hasSuffix ? fileName.left(dotIndex) : fileName;
    const QString suffix = hasSuffix ? fileName.mid(dotIndex + 1) : QString();

    const auto tryCandidate = [&](const QString &candidateName) {
        const QString candidate = QDir(baseDir).absoluteFilePath(candidateName);
        const QFileInfo info(candidate);
        if (info.exists() || info.isSymLink())
            return QString();
        if (!reserveTransferTarget(candidate, reservedPaths, kCaseInsensitiveLocalPaths))
            return QString();
        return candidate;
    };

    if (const QString original = tryCandidate(fileName); !original.isEmpty())
        return original;

    for (int i = 1; i < 10000; ++i)
    {
        const QString candidateName = suffix.isEmpty()
                                          ? QStringLiteral("%1 (%2)").arg(stem).arg(i)
                                          : QStringLiteral("%1 (%2).%3").arg(stem).arg(i).arg(suffix);
        if (const QString candidate = tryCandidate(candidateName); !candidate.isEmpty())
            return candidate;
    }

    while (true)
    {
        QString token = QUuid::createUuid().toString();
        token.remove(QLatin1Char('{'));
        token.remove(QLatin1Char('}'));
        const QString candidateName = suffix.isEmpty()
                                          ? QStringLiteral("%1 (%2)").arg(stem, token)
                                          : QStringLiteral("%1 (%2).%3").arg(stem, token, suffix);
        if (const QString candidate = tryCandidate(candidateName); !candidate.isEmpty())
            return candidate;
    }
}

#endif /* TRANSFER_PATH_UTIL_H */
