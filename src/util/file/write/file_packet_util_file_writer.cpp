#include "util/file/file_packet_util.h"

#include <QCoreApplication>
#include <QCryptographicHash>

#include <memory>

namespace
{
constexpr qint64 kCopyBufferSize = 64 * 1024;
} // namespace


bool FilePacketUtil::streamCopyFile(QFile &sourceFile, qint64 sourceOffset, const QString &targetPath, qint64 dataSize,
                                    QString *errorMessage, QString *sha256)
{
    if (errorMessage)
        errorMessage->clear();
    if (sha256)
        sha256->clear();
    std::unique_ptr<QCryptographicHash> fileHash;
    if (sha256)
        fileHash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    QFileInfo targetFileInfo(targetPath);
    if (!QDir().mkpath(targetFileInfo.absolutePath()))
    {
        LOG_ERROR("Failed to create target directory: {}", targetFileInfo.absolutePath());
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("FilePacketUtil", "Failed to create target directory: %1").arg(targetFileInfo.absolutePath());
        return false;
    }

    QSaveFile targetFile(targetPath);
    if (!targetFile.open(QIODevice::WriteOnly))
    {
        LOG_ERROR("Failed to create target file: {} error: {}", targetPath, targetFile.errorString());
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("FilePacketUtil", "Failed to create target file: %1 (%2)").arg(targetPath, targetFile.errorString());
        return false;
    }

    if (!sourceFile.seek(sourceOffset))
    {
        LOG_ERROR("Failed to seek source file to offset: {}", sourceOffset);
        targetFile.cancelWriting();
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("FilePacketUtil", "Failed to seek source file");
        return false;
    }

    QByteArray buffer;
    qint64 totalCopied = 0;
    qint64 remaining = dataSize;

    while (remaining > 0 && !sourceFile.atEnd())
    {
        const qint64 toRead = qMin(kCopyBufferSize, remaining);
        buffer = sourceFile.read(toRead);

        if (buffer.isEmpty())
            break;

        const qint64 written = targetFile.write(buffer);
        if (written != buffer.size())
        {
            LOG_ERROR("Failed to write to target file: {} written: {}, expected: {}",
                      targetPath, written, buffer.size());
            targetFile.cancelWriting();
            if (errorMessage)
                *errorMessage = QCoreApplication::translate("FilePacketUtil", "Failed to write target file: %1").arg(targetPath);
            return false;
        }

        totalCopied += written;
        remaining -= written;
        if (fileHash)
            fileHash->addData(buffer);
    }

    if (totalCopied != dataSize)
    {
        LOG_ERROR("File copy size mismatch: {} copied: {}, expected={}",
                  targetPath, totalCopied, dataSize);
        targetFile.cancelWriting();
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("FilePacketUtil", "File size mismatch while writing target: %1").arg(targetPath);
        return false;
    }

    if (!targetFile.commit())
    {
        LOG_ERROR("Failed to commit target file: {} error: {}", targetPath, targetFile.errorString());
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("FilePacketUtil", "Failed to commit target file: %1 (%2)").arg(targetPath, targetFile.errorString());
        return false;
    }

    QFileInfo checkFile(targetPath);
    checkFile.refresh();
    int retries = 0;
    while (!checkFile.exists() && retries < 10)
    {
        QThread::msleep(10);
        checkFile.refresh();
        ++retries;
    }

    if (!checkFile.exists())
    {
        LOG_ERROR("Target file does not exist after copy: {}", targetPath);
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("FilePacketUtil", "Target file does not exist after copy: %1").arg(targetPath);
        return false;
    }

    if (sha256 && fileHash)
        *sha256 = QString::fromLatin1(fileHash->result().toHex());

    return true;
}
