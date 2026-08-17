#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"
#include "util/text/convert_util.h"

namespace
{
constexpr quint32 kMaxFilePacketHeaderSize = 1024 * 1024;


struct FileDataPacketInfo
{
    QJsonObject header;
    QByteArray headerBytes;
    qint64 dataStart = 0;
    qint64 dataSize = 0;
};


bool readFileDataPacketHeader(QFile &tempFile, FileDataPacketInfo &out)
{
    if (tempFile.size() < 4)
    {
        LOG_ERROR("Temp file too small to contain header size");
        return false;
    }

    const QByteArray headerSizeBytes = tempFile.read(4);
    QDataStream stream(headerSizeBytes);
    stream.setByteOrder(QDataStream::BigEndian);
    quint32 headerSize = 0;
    stream >> headerSize;

    if (headerSize == 0 || headerSize > kMaxFilePacketHeaderSize || headerSize > tempFile.size() - 4)
    {
        LOG_ERROR("Invalid header size: {}, total file: {}", headerSize, tempFile.size());
        return false;
    }

    out.headerBytes = tempFile.read(headerSize);
    out.header = JsonUtil::safeParseObject(out.headerBytes);
    if (!JsonUtil::isValidObject(out.header))
    {
        LOG_ERROR("Failed to parse file data packet header");
        return false;
    }

    out.dataStart = 4 + headerSize;
    out.dataSize = tempFile.size() - out.dataStart;
    return true;
}

} // namespace


void FilePacketUtil::processFileDataPacket(const QString &tempFilePath)
{
    QFile tempFile(tempFilePath);
    if (!tempFile.open(QIODevice::ReadOnly))
    {
        LOG_ERROR("Failed to open temp file for processing: {}", tempFilePath);
        return;
    }

    FileDataPacketInfo packet;
    if (!readFileDataPacketHeader(tempFile, packet))
        return;

    const QString msgType = JsonUtil::getString(packet.header, Constant::KEY_MSGTYPE);
    const QString ctlPath = JsonUtil::getString(packet.header, Constant::KEY_PATH_CTL);
    const QString cliPath = JsonUtil::getString(packet.header, Constant::KEY_PATH_CLI);
    const QString transferId = JsonUtil::getString(packet.header, Constant::KEY_TRANSFER_ID);
    const bool isDirectory = JsonUtil::getBool(packet.header, "isDirectory", false);
    const bool directoryStart = JsonUtil::getBool(packet.header, "directoryStart", false);
    const bool directoryEnd = JsonUtil::getBool(packet.header, "directoryEnd", false);
    const bool transferStatus = JsonUtil::getBool(packet.header, "status", true);

    {
        QMutexLocker locker(&m_reassemblyMutex);
        cleanupExpiredCancelledTransfersLocked(QDateTime::currentMSecsSinceEpoch());
        if (!transferId.isEmpty() && m_cancelledTransfers.contains(transferId))
        {
            LOG_INFO("Drop cancelled file packet: transferId={}, cliPath={}, ctlPath={}",
                     transferId, cliPath, ctlPath);
            return;
        }
    }

    if (isDirectory)
    {
        const QString targetPath = (msgType == Constant::TYPE_FILE_DOWNLOAD) ? ctlPath : cliPath;
        if (!targetPath.isEmpty() && directoryStart)
        {
            if (!QDir().mkpath(targetPath))
            {
                LOG_ERROR("Failed to create target directory: {}", targetPath);
                if (msgType == Constant::TYPE_FILE_DOWNLOAD)
                    emit fileDownloadCompleted(false, targetPath);
                else if (msgType == Constant::TYPE_FILE_UPLOAD)
                    emit fileReceived(false, targetPath,
                                      QStringLiteral("Failed to create target directory: %1").arg(targetPath));
            }
            else
            {
                LOG_INFO("Created target directory: {}", targetPath);
            }
        }

        if (!targetPath.isEmpty() && directoryEnd)
        {
            if (msgType == Constant::TYPE_FILE_DOWNLOAD)
                emit fileDownloadCompleted(transferStatus, targetPath);
            else if (msgType == Constant::TYPE_FILE_UPLOAD)
                emit fileReceived(transferStatus, targetPath, transferStatus ? QString() : QStringLiteral("Directory upload failed"));
        }
    }
    else if (msgType == Constant::TYPE_FILE_DOWNLOAD && !ctlPath.isEmpty() && !cliPath.isEmpty())
    {
        QString errorMessage;
        if (streamCopyFile(tempFile, packet.dataStart, ctlPath, packet.dataSize, &errorMessage))
        {
            emit fileDownloadCompleted(true, ctlPath);
            LOG_INFO("Received file download: {} ({})", ctlPath, ConvertUtil::formatFileSize(packet.dataSize));
        }
        else
        {
            emit fileDownloadCompleted(false, ctlPath);
        }
    }
    else if (msgType == Constant::TYPE_FILE_UPLOAD && !ctlPath.isEmpty() && !cliPath.isEmpty())
    {
        QString errorMessage;
        QString sha256;
        if (streamCopyFile(tempFile, packet.dataStart, cliPath, packet.dataSize,
                           &errorMessage, &sha256))
        {
            emit fileReceived(true, cliPath, QString(), sha256);
            LOG_INFO("Received file upload: {} ({})", cliPath, ConvertUtil::formatFileSize(packet.dataSize));
        }
        else
        {
            emit fileReceived(false, cliPath, errorMessage);
        }
    }
    else
    {
        LOG_WARNING("Unknown file data packet type: {}, headerSize={} bytes", msgType, packet.headerBytes.size());
    }
}
