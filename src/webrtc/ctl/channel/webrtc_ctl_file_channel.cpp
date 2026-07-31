#include "webrtc/ctl/webrtc_ctl.h"

#include "util/file/file_packet_util.h"
#include "util/qt/qt_callback_util.h"

#include <QPointer>
#include <QThread>

namespace
{
constexpr qint64 kMaxFileIngressBytes = 32 * 1024 * 1024;
constexpr size_t kMaxFileIngressFragments = 8192;
constexpr size_t kFileIngressBatchSize = 512;
}


void WebRtcCtl::onFileChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onFileChannelOpen();
        });
        return;
    }
    const QString channelLabel = m_fileChannel ? QString::fromStdString(m_fileChannel->label()) : QString();
    LOG_INFO("File channel opened: {}", channelLabel);
}


void WebRtcCtl::onFileChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onFileChannelClosed();
        });
        return;
    }
    const QString channelLabel = m_fileChannel ? QString::fromStdString(m_fileChannel->label()) : QString();
    LOG_INFO("File channel closed: {}", channelLabel);
}


void WebRtcCtl::onFileChannelError(const std::string &error)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const std::string errorCopy = error;
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, errorCopy]() {
            if (guard)
                guard->onFileChannelError(errorCopy);
        });
        return;
    }
    LOG_ERROR("File channel error: {}", error);
}


void WebRtcCtl::onFileChannelMessage(const rtc::message_variant &message)
{
    if (m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (!std::holds_alternative<rtc::binary>(message))
    {
        LOG_WARN("File channel received text message, but should use file_text channel instead");
        return;
    }
    if (std::get<rtc::binary>(message).size() > FRAGMENT_SIZE)
    {
        LOG_WARN("Rejected oversized file channel fragment: size={} bytes", std::get<rtc::binary>(message).size());
        return;
    }
    rtc::binary binaryData = std::get<rtc::binary>(message);
    if (QThread::currentThread() == thread())
    {
        processFileChannelFragment(std::move(binaryData));
        return;
    }

    bool scheduleDrain = false;
    {
        QMutexLocker locker(&m_fileIngressMutex);
        while (!m_shutdownStarted.load() &&
               (m_fileIngressBytes + static_cast<qint64>(binaryData.size()) > kMaxFileIngressBytes ||
                m_fileIngress.size() >= kMaxFileIngressFragments))
        {
            m_fileIngressDrained.wait(&m_fileIngressMutex, 50);
        }
        if (m_shutdownStarted.load())
            return;

        m_fileIngressBytes += static_cast<qint64>(binaryData.size());
        m_fileIngress.emplace_back(std::move(binaryData));
        if (!m_fileIngressScheduled)
        {
            m_fileIngressScheduled = true;
            scheduleDrain = true;
        }
    }

    if (scheduleDrain)
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->drainFileIngress();
        });
    }
}


void WebRtcCtl::drainFileIngress()
{
    std::deque<rtc::binary> batch;
    bool scheduleNext = false;
    {
        QMutexLocker locker(&m_fileIngressMutex);
        if (m_shutdownStarted.load())
        {
            m_fileIngress.clear();
            m_fileIngressBytes = 0;
            m_fileIngressScheduled = false;
            m_fileIngressDrained.wakeAll();
            return;
        }

        const size_t batchSize = qMin(kFileIngressBatchSize, m_fileIngress.size());
        for (size_t i = 0; i < batchSize; ++i)
        {
            m_fileIngressBytes -= static_cast<qint64>(m_fileIngress.front().size());
            batch.emplace_back(std::move(m_fileIngress.front()));
            m_fileIngress.pop_front();
        }
        m_fileIngressDrained.wakeAll();
        scheduleNext = !m_fileIngress.empty();
        if (!scheduleNext)
            m_fileIngressScheduled = false;
    }

    for (rtc::binary &fragment : batch)
        processFileChannelFragment(std::move(fragment));

    if (scheduleNext)
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->drainFileIngress();
        });
    }
}


void WebRtcCtl::processFileChannelFragment(rtc::binary data)
{
    const QString channelLabel = m_fileChannel ? QString::fromStdString(m_fileChannel->label()) : QString();

    if (m_filePacketUtil)
        m_filePacketUtil->processReceivedFragment(data, channelLabel);
}
