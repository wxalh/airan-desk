#include "webrtc/ctl/webrtc_ctl.h"

#include <limits>

#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>


bool WebRtcCtl::sendFileMetadataPacket(const QJsonObject &header, const QString &transferId)
{
    if (!m_fileChannel || !m_fileChannel->isOpen())
    {
        LOG_ERROR("File channel not available for metadata packet");
        return false;
    }
    try
    {
        const auto cancelCallback = [this, transferId]() {
            return isTransferCancelled(transferId);
        };
        return FilePacketUtil::sendDataPacket(header, QByteArray(), m_fileChannel, FilePacketUtil::ProgressCallback(), cancelCallback);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception during metadata packet send: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Exception during metadata packet send: unknown error");
    }
    return false;
}


void WebRtcCtl::uploadDirectory(const QString &ctlPath, const QString &cliPath, const QString &transferId)
{
    QDir dir(ctlPath);
    if (!dir.exists())
    {
        LOG_ERROR("Directory does not exist: {}", ctlPath);
        emit recvUploadFileRes(false, cliPath, tr("Source directory does not exist."));
        return;
    }

    int totalFiles = 0;
    const qint64 totalBytes = collectDirectoryStats(ctlPath, &totalFiles);
    emitTransferProgress(transferId, 0, totalBytes, 0, totalFiles);

    QJsonObject dirStartHeader = JsonUtil::createObject()
                                     .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                     .add(Constant::KEY_PATH_CTL, ctlPath)
                                     .add(Constant::KEY_PATH_CLI, cliPath)
                                     .add(Constant::KEY_TRANSFER_ID, transferId)
                                     .add(Constant::KEY_TRANSFER_TOTAL_BYTES, static_cast<double>(totalBytes))
                                     .add(Constant::KEY_TRANSFER_TOTAL_FILES, totalFiles)
                                     .add("isDirectory", true)
                                     .add("directoryStart", true)
                                     .build();
    if (!sendFileMetadataPacket(dirStartHeader, transferId))
    {
        emit recvUploadFileRes(false, cliPath, tr("Failed to send directory metadata."));
        return;
    }

    int fileCount = 0;
    qint64 transferredBytes = 0;
    bool hasErrors = false;

    QDirIterator dirIt(ctlPath,
                       QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                       QDirIterator::Subdirectories);
    while (dirIt.hasNext())
    {
        if (isTransferCancelled(transferId))
            return;

        const QFileInfo dirInfo(dirIt.next());
        if (dirInfo.isSymLink())
            continue;
        const QString relativePath = dir.relativeFilePath(dirInfo.absoluteFilePath());
        const QString targetPath = QDir::cleanPath(cliPath + "/" + relativePath);
        const QJsonObject directoryHeader = JsonUtil::createObject()
                                                .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                                .add(Constant::KEY_PATH_CTL, dirInfo.absoluteFilePath())
                                                .add(Constant::KEY_PATH_CLI, targetPath)
                                                .add(Constant::KEY_TRANSFER_ID, transferId)
                                                .add("isDirectory", true)
                                                .add("directoryStart", true)
                                                .build();
        if (!sendFileMetadataPacket(directoryHeader, transferId))
            hasErrors = true;
    }

    QDirIterator it(ctlPath,
                    QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        if (isTransferCancelled(transferId))
        {
            LOG_INFO("Upload directory cancelled: {}", ctlPath);
            return;
        }

        const QString filePath = it.next();
        QFileInfo fileInfo(filePath);
        QString relativePath = dir.relativeFilePath(fileInfo.absoluteFilePath());
        QString fullRemotePath = QDir::cleanPath(cliPath + "/" + relativePath);
        if (uploadSingleFile(fileInfo.absoluteFilePath(), fullRemotePath, transferId,
                             transferredBytes, totalBytes, fileCount + 1, totalFiles))
        {
            fileCount++;
            const qint64 fileSize = qMax<qint64>(0, fileInfo.size());
            transferredBytes = transferredBytes <= (std::numeric_limits<qint64>::max)() - fileSize
                                   ? transferredBytes + fileSize
                                   : (std::numeric_limits<qint64>::max)();
        }
        else
        {
            if (isTransferCancelled(transferId))
                return;
            hasErrors = true;
        }
    }

    if (fileCount == 0)
        LOG_INFO("Directory has no files, sent directory metadata only: {}", ctlPath);
    else
        LOG_INFO("Uploaded directory: {} -> {} ({} files, hasErrors={})", ctlPath, cliPath, fileCount, hasErrors);

    QJsonObject dirEndHeader = JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                   .add(Constant::KEY_PATH_CTL, ctlPath)
                                   .add(Constant::KEY_PATH_CLI, cliPath)
                                   .add(Constant::KEY_TRANSFER_ID, transferId)
                                   .add(Constant::KEY_TRANSFER_TOTAL_BYTES, static_cast<double>(totalBytes))
                                   .add(Constant::KEY_TRANSFER_TOTAL_FILES, totalFiles)
                                   .add("isDirectory", true)
                                   .add("directoryEnd", true)
                                   .add("fileCount", fileCount)
                                   .add("status", !hasErrors)
                                   .build();
    const bool completionSent = sendFileMetadataPacket(dirEndHeader, transferId);
    if (!completionSent)
    {
        emit recvUploadFileRes(false, cliPath, tr("Failed to send directory completion metadata."));
        return;
    }

    LOG_INFO("Sent directory: {} -> {} ({} files)", ctlPath, cliPath, fileCount);
    emitTransferProgress(transferId, totalBytes, totalBytes, totalFiles, totalFiles);
}
