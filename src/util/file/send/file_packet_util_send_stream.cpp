#include "util/file/file_packet_util.h"
#include "util/file/stream/file_packet_util_stream_helpers.h"
#include "util/json/json_util.h"
#include "util/text/convert_util.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <chrono>

namespace
{
struct AsyncFragmentSendState
{
    std::mutex mutex;
    std::condition_variable condition;
    bool completed{false};
    bool success{false};
};

bool sendFragmentAndWait(const std::shared_ptr<rtc::DataChannel> &channel,
                         const rtc::binary &fragment,
                         const QString &logPath,
                         const FilePacketUtil::CancelCallback &cancelCallback)
{
    if (!channel || !channel->isOpen())
        return false;

    constexpr auto kSendTimeout = std::chrono::seconds(45);
    const auto state = std::make_shared<AsyncFragmentSendState>();
    channel->sendAsync(rtc::message_variant(fragment), [state](bool success) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->success = success;
            state->completed = true;
        }
        state->condition.notify_one();
    });

    const auto deadline = std::chrono::steady_clock::now() + kSendTimeout;
    std::unique_lock<std::mutex> lock(state->mutex);
    while (!state->completed)
    {
        if (state->condition.wait_for(lock, std::chrono::milliseconds(10), [&state]() { return state->completed; }))
            break;

        lock.unlock();
        if (!channel->isOpen())
        {
            LOG_ERROR("Data channel closed while waiting for asynchronous packet fragment send: {}", logPath);
            return false;
        }
        const bool cancelled = cancelCallback && cancelCallback();
        lock.lock();
        if (cancelled)
        {
            LOG_INFO("Packet fragment send cancelled: {}", logPath);
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            LOG_ERROR("Timed out waiting for asynchronous packet fragment send: {}", logPath);
            return false;
        }
    }
    return state->completed && state->success;
}
}

bool FilePacketUtil::sendPacketStream(QFile *file,
                                      const QByteArray &payload,
                                      const QJsonObject &header,
                                      std::shared_ptr<rtc::DataChannel> channel,
                                      const QString &logPath,
                                      const ProgressCallback &progressCallback,
                                      const CancelCallback &cancelCallback,
                                      QCryptographicHash *fileHash)
{
    if (!channel || !channel->isOpen())
    {
        LOG_ERROR("Channel not available for packet streaming");
        return false;
    }

    const QByteArray headerBytes = JsonUtil::toCompactBytes(header);
    constexpr int kMaxFilePacketHeaderSize = 1024 * 1024;
    if (headerBytes.isEmpty() || headerBytes.size() > kMaxFilePacketHeaderSize)
    {
        LOG_ERROR("Invalid file packet header size: {}", headerBytes.size());
        return false;
    }
    const qint64 fileSize = file ? file->size() : 0;
    if (fileSize < 0 || static_cast<quint64>(fileSize) > MAX_REASONABLE_OFFSET)
    {
        LOG_ERROR("Invalid file packet source size: {}", fileSize);
        return false;
    }
    QByteArray headerSizeBytes;
    QDataStream headerStream(&headerSizeBytes, QIODevice::WriteOnly);
    headerStream.setByteOrder(QDataStream::BigEndian);
    headerStream << static_cast<quint32>(headerBytes.size());

    quint64 totalDataSize = 4 + headerBytes.size() + payload.size();
    if (file)
        totalDataSize += static_cast<quint64>(fileSize);
    if (totalDataSize > MAX_REASONABLE_OFFSET)
    {
        LOG_ERROR("File packet exceeds maximum supported size: {}", totalDataSize);
        return false;
    }
    quint64 totalFragments = (totalDataSize + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
    if (totalFragments == 0)
        totalFragments = 1;

    LOG_INFO("Starting stream send for packet: {} ({}, {} fragments)",
             logPath,
             ConvertUtil::formatFileSize(totalDataSize),
             totalFragments);

    const QUuid messageId = QUuid::createUuid();
    const QByteArray messageIdBytes = messageId.toRfc4122();
    LOG_DEBUG("Generated message ID: {}", messageId.toString());

    QByteArray dataBuffer;
    dataBuffer.append(headerSizeBytes);
    dataBuffer.append(headerBytes);
    dataBuffer.append(payload);

    quint64 fragmentIndex = 0;
    quint64 totalSent = 0;
    while (fragmentIndex < totalFragments)
    {
        if (cancelCallback && cancelCallback())
        {
            LOG_INFO("Packet stream send cancelled: {}", logPath);
            if (file)
                file->close();
            return false;
        }

        const QByteArray fragmentPayload =
            FilePacketStreamHelpers::takeFragmentPayload(dataBuffer, file, fileHash);
        if (fragmentPayload.isEmpty())
            break;

        const rtc::binary fragment = FilePacketStreamHelpers::makeFragment(messageIdBytes,
                                                                           totalFragments,
                                                                           fragmentIndex,
                                                                           fragmentPayload);

        try
        {
            if (!waitForChannelBackpressure(channel, logPath, cancelCallback))
            {
                if (file)
                    file->close();
                return false;
            }
            if (!sendFragmentAndWait(channel, fragment, logPath, cancelCallback))
            {
                LOG_ERROR("Asynchronous data channel rejected file fragment: index={}, buffered={} bytes",
                          fragmentIndex, channel->bufferedAmount());
                if (file)
                    file->close();
                return false;
            }
            totalSent += fragmentPayload.size();
            if (progressCallback && (fragmentIndex % 16 == 0 || fragmentIndex == totalFragments - 1))
                progressCallback(static_cast<qint64>(totalSent), static_cast<qint64>(totalDataSize));

            if (fragmentIndex % 1024 == 0 || fragmentIndex == totalFragments - 1)
            {
                LOG_DEBUG("Sent fragment {}/{} ({}) - MessageID: {}",
                          fragmentIndex + 1,
                          totalFragments,
                          ConvertUtil::formatFileSize(totalSent),
                          messageId.toString());
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to send fragment {}: {}", fragmentIndex, e.what());
            if (file)
                file->close();
            return false;
        }

        ++fragmentIndex;
    }

    if (file)
        file->close();

    if (fragmentIndex != totalFragments || totalSent != totalDataSize)
    {
        LOG_ERROR("Packet stream send incomplete: {} sent={} expected={} fragments={}/{}",
                  logPath,
                  ConvertUtil::formatFileSize(totalSent),
                  ConvertUtil::formatFileSize(totalDataSize),
                  fragmentIndex,
                  totalFragments);
        return false;
    }

    LOG_INFO("Successfully sent packet stream: {} ({}, {} fragments)",
             logPath,
             ConvertUtil::formatFileSize(totalDataSize),
             totalFragments);
    return true;
}
