#include "media/codec/openh264/openh264_staging_internal.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>

#include <array>

namespace airan::media::openh264::detail
{

StagedSourceFile stageSourceFile(const QString &sourcePath,
                                 const QString &stagingDirectory,
                                 const SnapshotCompleteHook &snapshotComplete,
                                 const QString &fileNameSuffix)
{
    StagedSourceFile staged;
    if (!QDir().mkpath(stagingDirectory))
    {
        staged.reasonCode = QStringLiteral("storage-unavailable");
        return staged;
    }
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
    {
        staged.reasonCode = QStringLiteral("unreadable");
        return staged;
    }

    QString stageTemplate =
        QDir(stagingDirectory).filePath(QStringLiteral(".airan-openh264-XXXXXX"));
    if (!fileNameSuffix.isEmpty())
        stageTemplate += QLatin1Char('-') + fileNameSuffix;
    staged.file = std::make_unique<QTemporaryFile>(stageTemplate);
    staged.file->setAutoRemove(true);
    if (!staged.file->open())
    {
        staged.file.reset();
        staged.reasonCode = QStringLiteral("copy-failed");
        return staged;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> buffer{};
    while (!source.atEnd())
    {
        const qint64 count =
            source.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (count < 0 ||
            (count > 0 && staged.file->write(buffer.data(), count) != count))
        {
            staged.file.reset();
            staged.reasonCode = QStringLiteral("copy-failed");
            return staged;
        }
        if (count <= 0)
            continue;
        if (staged.header.size() < 4096)
        {
            const qint64 headerCount =
                qMin<qint64>(count, 4096 - staged.header.size());
            staged.header.append(buffer.data(), static_cast<int>(headerCount));
        }
        hash.addData(buffer.data(), count);
        staged.size += count;
    }
    source.close();
    if (!staged.file->flush())
    {
        staged.file.reset();
        staged.reasonCode = QStringLiteral("copy-failed");
        return staged;
    }
    staged.sha256 = hash.result();
    if (snapshotComplete)
        snapshotComplete();
    if (!staged.file->seek(0))
    {
        staged.file.reset();
        staged.reasonCode = QStringLiteral("copy-failed");
    }
    return staged;
}

} // namespace airan::media::openh264::detail
