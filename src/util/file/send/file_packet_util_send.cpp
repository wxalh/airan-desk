#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"

#include <QCryptographicHash>
#include <QRunnable>
#include <QThreadPool>

#include <memory>
#include <utility>

namespace
{
class AsyncFileStreamTask final : public QRunnable
{
public:
    AsyncFileStreamTask(QString filePath,
                        QJsonObject header,
                        std::shared_ptr<rtc::DataChannel> channel,
                        FilePacketUtil::ProgressCallback progressCallback,
                        FilePacketUtil::CancelCallback cancelCallback,
                        FilePacketUtil::CompletionCallback completionCallback,
                        bool calculateSha256)
        : m_filePath(std::move(filePath)),
          m_header(std::move(header)),
          m_channel(std::move(channel)),
          m_progressCallback(std::move(progressCallback)),
          m_cancelCallback(std::move(cancelCallback)),
          m_completionCallback(std::move(completionCallback)),
          m_calculateSha256(calculateSha256)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QString sha256;
        bool success = false;
        try
        {
            success = FilePacketUtil::sendFileStream(
                m_filePath,
                m_header,
                m_channel,
                m_progressCallback,
                m_cancelCallback,
                m_calculateSha256 ? &sha256 : nullptr);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Unhandled exception in asynchronous file stream task: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Unhandled exception in asynchronous file stream task");
        }
        if (m_completionCallback)
            m_completionCallback(success, sha256);
    }

private:
    QString m_filePath;
    QJsonObject m_header;
    std::shared_ptr<rtc::DataChannel> m_channel;
    FilePacketUtil::ProgressCallback m_progressCallback;
    FilePacketUtil::CancelCallback m_cancelCallback;
    FilePacketUtil::CompletionCallback m_completionCallback;
    bool m_calculateSha256{false};
};

class AsyncDataPacketTask final : public QRunnable
{
public:
    AsyncDataPacketTask(QJsonObject header,
                        QByteArray payload,
                        std::shared_ptr<rtc::DataChannel> channel,
                        FilePacketUtil::ProgressCallback progressCallback,
                        FilePacketUtil::CancelCallback cancelCallback,
                        FilePacketUtil::CompletionCallback completionCallback)
        : m_header(std::move(header)),
          m_payload(std::move(payload)),
          m_channel(std::move(channel)),
          m_progressCallback(std::move(progressCallback)),
          m_cancelCallback(std::move(cancelCallback)),
          m_completionCallback(std::move(completionCallback))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        bool success = false;
        try
        {
            success = FilePacketUtil::sendDataPacket(m_header,
                                                     m_payload,
                                                     m_channel,
                                                     m_progressCallback,
                                                     m_cancelCallback);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Unhandled exception in asynchronous file metadata task: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Unhandled exception in asynchronous file metadata task");
        }
        if (m_completionCallback)
            m_completionCallback(success, QString());
    }

private:
    QJsonObject m_header;
    QByteArray m_payload;
    std::shared_ptr<rtc::DataChannel> m_channel;
    FilePacketUtil::ProgressCallback m_progressCallback;
    FilePacketUtil::CancelCallback m_cancelCallback;
    FilePacketUtil::CompletionCallback m_completionCallback;
};
}

bool FilePacketUtil::sendFileStream(const QString &filePath,
                                    const QJsonObject &header,
                                    std::shared_ptr<rtc::DataChannel> channel,
                                    const ProgressCallback &progressCallback,
                                    const CancelCallback &cancelCallback,
                                    QString *sha256)
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

    if (sha256)
        sha256->clear();
    std::unique_ptr<QCryptographicHash> fileHash;
    if (sha256)
        fileHash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    const bool sent = sendPacketStream(&file, QByteArray(), header, channel, filePath,
                                       progressCallback, cancelCallback, fileHash.get());
    if (sent && sha256 && fileHash)
        *sha256 = QString::fromLatin1(fileHash->result().toHex());
    return sent;
}


void FilePacketUtil::sendFileStreamAsync(const QString &filePath,
                                         const QJsonObject &header,
                                         std::shared_ptr<rtc::DataChannel> channel,
                                         const ProgressCallback &progressCallback,
                                         const CancelCallback &cancelCallback,
                                         const CompletionCallback &completionCallback,
                                         bool calculateSha256)
{
    if (!completionCallback)
        return;

    auto *task = new AsyncFileStreamTask(filePath,
                                         header,
                                         std::move(channel),
                                         progressCallback,
                                         cancelCallback,
                                         completionCallback,
                                         calculateSha256);
    QThreadPool::globalInstance()->start(task);
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
                            cancelCallback,
                            nullptr);
}


void FilePacketUtil::sendDataPacketAsync(const QJsonObject &header,
                                         const QByteArray &payload,
                                         std::shared_ptr<rtc::DataChannel> channel,
                                         const ProgressCallback &progressCallback,
                                         const CancelCallback &cancelCallback,
                                         const CompletionCallback &completionCallback)
{
    if (!completionCallback)
        return;

    QThreadPool::globalInstance()->start(new AsyncDataPacketTask(header,
                                                                  payload,
                                                                  std::move(channel),
                                                                  progressCallback,
                                                                  cancelCallback,
                                                                  completionCallback));
}
