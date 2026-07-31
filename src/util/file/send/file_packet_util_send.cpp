#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"


bool FilePacketUtil::sendFileStream(const QString &filePath,
                                    const QJsonObject &header,
                                    std::shared_ptr<rtc::DataChannel> channel,
                                    const ProgressCallback &progressCallback,
                                    const CancelCallback &cancelCallback)
{
    if (!channel || !channel->isOpen())
    {
        LOG_ERROR("Channel not available for file streaming");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        LOG_ERROR("Failed to open file for streaming: {} error: {}", filePath, file.errorString());
        return false;
    }

    return sendPacketStream(&file, QByteArray(), header, channel, filePath, progressCallback, cancelCallback);
}


bool FilePacketUtil::sendDataPacket(const QJsonObject &header,
                                    const QByteArray &payload,
                                    std::shared_ptr<rtc::DataChannel> channel,
                                    const ProgressCallback &progressCallback,
                                    const CancelCallback &cancelCallback)
{
    return sendPacketStream(nullptr,
                            payload,
                            header,
                            channel,
                            JsonUtil::getString(header, Constant::KEY_PATH_CLI, QStringLiteral("metadata")),
                            progressCallback,
                            cancelCallback);
}
