#pragma once

#include <QByteArray>
#include <QString>
#include <QTemporaryFile>

#include <functional>
#include <memory>

namespace airan::media::openh264::detail
{

using SnapshotCompleteHook = std::function<void()>;

struct StagedSourceFile
{
    std::unique_ptr<QTemporaryFile> file;
    qint64 size{0};
    QByteArray header;
    QByteArray sha256;
    QString reasonCode;
};

StagedSourceFile stageSourceFile(
    const QString &sourcePath,
    const QString &stagingDirectory,
    const SnapshotCompleteHook &snapshotComplete = SnapshotCompleteHook(),
    const QString &fileNameSuffix = QString());

} // namespace airan::media::openh264::detail
