#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/text/convert_util.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"
#include "security/audit_session.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>


bool WebRtcCli::sendSingleFile(const QString &cliPath, const QString &ctlPath, const QString &transferId,
                               qint64 baseBytes, qint64 totalBytes, int currentFileIndex, int totalFiles)
{
    QFileInfo fileInfo(cliPath);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        LOG_ERROR("File does not exist or is not a regular file: {}", cliPath);
        sendFileErrorResponse(cliPath, "File does not exist or is not a regular file");
        if (m_auditSession)
            m_auditSession->recordFileTransfer(cliPath, 0, QStringLiteral("download"), QString(), false);
        return false;
    }

    const QString checksum = m_auditSession ? AuditSession::sha256ForFile(cliPath) : QString();

    const QString absCtlPath = QDir::cleanPath(ctlPath);

    QJsonObject header = JsonUtil::createObject()
                             .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                             .add(Constant::KEY_PATH_CLI, cliPath)
                             .add(Constant::KEY_PATH_CTL, absCtlPath)
                             .add(Constant::KEY_TRANSFER_ID, transferId)
                             .add(Constant::KEY_FILE_SIZE, static_cast<double>(fileInfo.size()))
                             .add(Constant::KEY_TRANSFER_TOTAL_BYTES, static_cast<double>(totalBytes >= 0 ? totalBytes : fileInfo.size()))
                             .add(Constant::KEY_TRANSFER_TOTAL_FILES, totalFiles)
                             .add("transferBaseBytes", static_cast<double>(baseBytes))
                             .add("transferFileIndex", currentFileIndex)
                             .add("isDirectory", false)
                             .build();

    if (m_fileChannel && m_fileChannel->isOpen())
    {
        try
        {
            const qint64 effectiveTotalBytes = totalBytes >= 0 ? totalBytes : fileInfo.size();
            auto lastProgressMs = std::make_shared<qint64>(0);
            const auto progressCallback = [this, transferId, baseBytes, effectiveTotalBytes, currentFileIndex, totalFiles,
                                           fileSize = fileInfo.size(), lastProgressMs, absCtlPath, cliPath](qint64 sentBytes, qint64 packetTotalBytes) {
                const qint64 headerBytes = qMax<qint64>(0, packetTotalBytes - fileSize);
                const qint64 currentFileBytes = qBound<qint64>(0, sentBytes - headerBytes, fileSize);
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (currentFileBytes < fileSize && nowMs - *lastProgressMs < 120)
                    return;
                *lastProgressMs = nowMs;
                sendTransferProgress(transferId,
                                     qMin(baseBytes + currentFileBytes, effectiveTotalBytes),
                                     effectiveTotalBytes,
                                     currentFileBytes >= fileSize ? currentFileIndex : qMax(0, currentFileIndex - 1),
                                     totalFiles,
                                     absCtlPath,
                                     cliPath);
            };
            const auto cancelCallback = [this, transferId]() {
                return isTransferCancelled(transferId);
            };

            if (FilePacketUtil::sendFileStream(cliPath, header, m_fileChannel, progressCallback, cancelCallback))
            {
                LOG_INFO("Sent file stream: {} -> {} ({})",
                         cliPath, absCtlPath, ConvertUtil::formatFileSize(fileInfo.size()));
                if (m_auditSession)
                    m_auditSession->recordFileTransfer(cliPath, fileInfo.size(), QStringLiteral("download"), checksum, true);
                return true;
            }

            LOG_ERROR("Failed to send file stream: {}", cliPath);
            if (!isTransferCancelled(transferId))
                sendFileErrorResponse(cliPath, "Failed to send file stream");
            if (m_auditSession)
                m_auditSession->recordFileTransfer(cliPath, fileInfo.size(), QStringLiteral("download"), checksum, false);
            return false;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception during file stream send: {}", e.what());
            if (!isTransferCancelled(transferId))
                sendFileErrorResponse(cliPath, "Exception during file stream send");
            if (m_auditSession)
                m_auditSession->recordFileTransfer(cliPath, fileInfo.size(), QStringLiteral("download"), checksum, false);
            return false;
        }
        catch (...)
        {
            LOG_ERROR("Failed to send file stream: unknown error");
            if (!isTransferCancelled(transferId))
                sendFileErrorResponse(cliPath, "Exception during file stream send");
            if (m_auditSession)
                m_auditSession->recordFileTransfer(cliPath, fileInfo.size(), QStringLiteral("download"), checksum, false);
            return false;
        }
    }

    LOG_ERROR("File channel not available for sending file");
    sendFileErrorResponse(cliPath, "File channel not available");
    if (m_auditSession)
        m_auditSession->recordFileTransfer(cliPath, fileInfo.size(), QStringLiteral("download"), checksum, false);
    return false;
}
