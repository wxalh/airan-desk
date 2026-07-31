#include "util/file/file_packet_util.h"

#include <QCoreApplication>
#include <QEventLoop>

#include <chrono>

namespace
{
constexpr size_t kFileChannelHighWatermark = 1536 * 1024;
constexpr size_t kFileChannelLowWatermark = 512 * 1024;
constexpr int kBackpressureSleepMs = 1;
constexpr int kBackpressureTimeoutMs = 45000;
} // namespace


bool FilePacketUtil::waitForChannelBackpressure(const std::shared_ptr<rtc::DataChannel> &channel,
                                                const QString &filePath,
                                                const CancelCallback &cancelCallback)
{
    if (cancelCallback && cancelCallback())
    {
        LOG_INFO("File channel send cancelled before backpressure wait: {}", filePath);
        return false;
    }

    if (!channel || !channel->isOpen())
    {
        LOG_ERROR("File channel closed while streaming: {}", filePath);
        return false;
    }

    if (channel->bufferedAmount() < kFileChannelHighWatermark)
        return true;

    const auto start = std::chrono::steady_clock::now();
    while (channel && channel->isOpen() && channel->bufferedAmount() > kFileChannelLowWatermark)
    {
        if (cancelCallback && cancelCallback())
        {
            LOG_INFO("File channel send cancelled while waiting for backpressure: {}", filePath);
            return false;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs > kBackpressureTimeoutMs)
        {
            LOG_ERROR("Timed out waiting for file channel backpressure to drain: {}, buffered={} bytes",
                      filePath,
                      channel->bufferedAmount());
            return false;
        }
        if (QCoreApplication::instance())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        QThread::msleep(kBackpressureSleepMs);
    }

    if (!channel || !channel->isOpen())
    {
        LOG_ERROR("File channel closed while waiting for backpressure: {}", filePath);
        return false;
    }
    return true;
}
