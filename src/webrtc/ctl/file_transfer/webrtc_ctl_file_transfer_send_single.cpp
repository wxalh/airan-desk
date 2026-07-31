#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"
#include "util/text/convert_util.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"

#include <QDateTime>
#include <QFileInfo>


bool WebRtcCtl::uploadSingleFile(const QString &ctlPath, const QString &cliPath, const QString &transferId,
                                 qint64 baseBytes, qint64 totalBytes, int currentFileIndex, int totalFiles)
{
    QFileInfo fileInfo(ctlPath);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        LOG_ERROR("File does not exist or is not a regular file: {}", ctlPath);
        emit recvUploadFileRes(false, cliPath, tr("Source file does not exist or is not a regular file."));
        return false;
    }

    QJsonObject header = JsonUtil::createObject()
                             .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                             .add(Constant::KEY_PATH_CTL, ctlPath)
                             .add(Constant::KEY_PATH_CLI, cliPath)
                             .add(Constant::KEY_TRANSFER_ID, transferId)
                             .add(Constant::KEY_FILE_SIZE, static_cast<double>(fileInfo.size()))
                             .add("isDirectory", false)
                             .build();

    if (!m_fileChannel || !m_fileChannel->isOpen())
    {
        LOG_ERROR("File channel not available for uploading file");
        emit recvUploadFileRes(false, cliPath, tr("File channel is not available."));
        return false;
    }

    try
    {
        const qint64 effectiveTotalBytes = totalBytes >= 0 ? totalBytes : fileInfo.size();
        auto lastProgressMs = std::make_shared<qint64>(0);
        const auto progressCallback = [this, transferId, baseBytes, effectiveTotalBytes, currentFileIndex, totalFiles,
                                       fileSize = fileInfo.size(), lastProgressMs](qint64 sentBytes, qint64 packetTotalBytes) {
            const qint64 headerBytes = qMax<qint64>(0, packetTotalBytes - fileSize);
            const qint64 currentFileBytes = qBound<qint64>(0, sentBytes - headerBytes, fileSize);
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (currentFileBytes < fileSize && nowMs - *lastProgressMs < 120)
                return;
            *lastProgressMs = nowMs;
            emitTransferProgress(transferId,
                                 qMin(baseBytes + currentFileBytes, effectiveTotalBytes),
                                 effectiveTotalBytes,
                                 currentFileBytes >= fileSize ? currentFileIndex : qMax(0, currentFileIndex - 1),
                                 totalFiles);
        };
        const auto cancelCallback = [this, transferId]() {
            return isTransferCancelled(transferId);
        };

        if (FilePacketUtil::sendFileStream(ctlPath, header, m_fileChannel, progressCallback, cancelCallback))
        {
            LOG_INFO("Sent file stream: {} -> {} ({})",
                     ctlPath, cliPath, ConvertUtil::formatFileSize(fileInfo.size()));
            return true;
        }

        LOG_ERROR("Failed to send file stream: {}", ctlPath);
        if (!isTransferCancelled(transferId))
            emit recvUploadFileRes(false, cliPath, tr("Failed to send file stream."));
        return false;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception during file stream send: {}", e.what());
        if (!isTransferCancelled(transferId))
            emit recvUploadFileRes(false, cliPath, tr("Exception during file stream send."));
        return false;
    }
    catch (...)
    {
        LOG_ERROR("Failed to send file stream: unknown error");
        if (!isTransferCancelled(transferId))
            emit recvUploadFileRes(false, cliPath, tr("Unknown exception during file stream send."));
        return false;
    }
}
