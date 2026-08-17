#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QPointer>

namespace
{
constexpr size_t kMaxFileTextMessageBytes = 16 * 1024 * 1024;
constexpr qint64 kMaxFileTextIngressBytes = 64LL * 1024 * 1024;
constexpr int kMaxFileTextIngressMessages = 8192;
constexpr int kFileTextIngressBatchSize = 256;
constexpr qint64 kMaxFileIngressBytes = 32 * 1024 * 1024;
constexpr size_t kMaxFileIngressFragments = 8192;
constexpr size_t kFileIngressBatchSize = 512;
}


void WebRtcCli::setupFileChannelCallbacks()
{
    if (!m_fileChannel)
        return;

    m_fileChannel->onOpen(makeWeakCallback(this, &WebRtcCli::onFileChannelOpen, m_callbackLifetime));
    m_fileChannel->onMessage(makeWeakCallback(this, &WebRtcCli::onFileChannelMessage, m_callbackLifetime));
    m_fileChannel->onError(makeWeakCallback(this, &WebRtcCli::onFileChannelError, m_callbackLifetime));
    m_fileChannel->onClosed(makeWeakCallback(this, &WebRtcCli::onFileChannelClosed, m_callbackLifetime));
}


void WebRtcCli::setupFileTextChannelCallbacks()
{
    if (!m_fileTextChannel)
        return;

    m_fileTextChannel->onOpen(makeWeakCallback(this, &WebRtcCli::onFileTextChannelOpen, m_callbackLifetime));
    m_fileTextChannel->onMessage(makeWeakCallback(this, &WebRtcCli::onFileTextChannelMessage, m_callbackLifetime));
    m_fileTextChannel->onError(makeWeakCallback(this, &WebRtcCli::onFileTextChannelError, m_callbackLifetime));
    m_fileTextChannel->onClosed(makeWeakCallback(this, &WebRtcCli::onFileTextChannelClosed, m_callbackLifetime));
}


void WebRtcCli::setupClipboardChannelCallbacks()
{
    if (!m_clipboardChannel)
        return;

    m_clipboardChannel->onOpen(makeWeakCallback(this, &WebRtcCli::onClipboardChannelOpen, m_callbackLifetime));
    m_clipboardChannel->onMessage(makeWeakCallback(this, &WebRtcCli::onClipboardChannelMessage, m_callbackLifetime));
    m_clipboardChannel->onError(makeWeakCallback(this, &WebRtcCli::onClipboardChannelError, m_callbackLifetime));
    m_clipboardChannel->onClosed(makeWeakCallback(this, &WebRtcCli::onClipboardChannelClosed, m_callbackLifetime));
}


void WebRtcCli::onFileChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onFileChannelOpen();
        });
        return;
    }
    LOG_INFO("File channel opened");
    m_fileIngressOverflowed.store(false);
    if (m_filePacketUtil)
        m_filePacketUtil->clearPendingReassemblies();
}


void WebRtcCli::onFileChannelMessage(rtc::message_variant data)
{
    if (m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (!std::holds_alternative<rtc::binary>(data))
    {
        LOG_WARN("File channel received text message, ignoring");
        return;
    }
    if (std::get<rtc::binary>(data).size() > FRAGMENT_SIZE)
    {
        LOG_WARN("Rejected oversized file channel fragment: size={} bytes", std::get<rtc::binary>(data).size());
        return;
    }
    rtc::binary binaryData = std::move(std::get<rtc::binary>(data));
    if (QThread::currentThread() == thread())
    {
        processFileChannelFragment(std::move(binaryData));
        return;
    }

    bool scheduleDrain = false;
    bool overflowed = false;
    {
        QMutexLocker locker(&m_fileIngressMutex);
        if (m_shutdownStarted.load())
            return;

        overflowed = m_fileIngressBytes + static_cast<qint64>(binaryData.size()) > kMaxFileIngressBytes ||
                     m_fileIngress.size() >= kMaxFileIngressFragments;
        if (!overflowed)
        {
            m_fileIngressBytes += static_cast<qint64>(binaryData.size());
            m_fileIngress.emplace_back(std::move(binaryData));
            if (!m_fileIngressScheduled)
            {
                m_fileIngressScheduled = true;
                scheduleDrain = true;
            }
        }
    }

    if (overflowed)
    {
        if (m_fileIngressOverflowed.exchange(true))
            return;
        LOG_ERROR("File ingress queue is full; closing file channel to abort the transfer without blocking WebRTC callbacks");
        if (m_fileChannel)
            m_fileChannel->close();
        if (m_filePacketUtil)
            m_filePacketUtil->clearPendingReassemblies();
        return;
    }

    if (scheduleDrain)
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->drainFileIngress();
        });
    }
}


void WebRtcCli::drainFileIngress()
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
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->drainFileIngress();
        });
    }
}


void WebRtcCli::processFileChannelFragment(rtc::binary data)
{
    if (m_filePacketUtil)
        m_filePacketUtil->processReceivedFragment(data, "file");
}


void WebRtcCli::onFileChannelError(std::string error)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard, error = std::move(error)]() mutable {
            if (guard)
                guard->onFileChannelError(std::move(error));
        });
        return;
    }
    LOG_ERROR("File channel error: {}", error);
    m_disconnectReason = QStringLiteral("file_channel_error");
    emit destroyCli();
}


void WebRtcCli::onFileChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onFileChannelClosed();
        });
        return;
    }
    LOG_INFO("File channel closed");
    {
        QMutexLocker locker(&m_fileIngressMutex);
        m_fileIngress.clear();
        m_fileIngressBytes = 0;
        m_fileIngressScheduled = false;
        m_fileIngressDrained.wakeAll();
    }
    if (m_filePacketUtil)
        m_filePacketUtil->clearPendingReassemblies();
    m_disconnectReason = QStringLiteral("file_channel_closed");
    emit destroyCli();
}


void WebRtcCli::onFileTextChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onFileTextChannelOpen();
        });
        return;
    }
    LOG_INFO("File text channel opened");
    m_fileTextIngressOverflowed.store(false);
    flushPendingFileTextMessages();
    if (m_terminalBackpressureTimer &&
        (!m_pendingTerminalOutputChunks.isEmpty() || m_terminalChannelPaused))
        m_terminalBackpressureTimer->start();
    populateLocalFiles();
}


void WebRtcCli::onFileTextChannelMessage(rtc::message_variant data)
{
    if (m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (!std::holds_alternative<std::string>(data))
    {
        LOG_WARN("File text channel received binary data, ignoring");
        return;
    }
    if (std::get<std::string>(data).size() > kMaxFileTextMessageBytes)
    {
        LOG_WARN("Rejected oversized file text channel message: size={} bytes", std::get<std::string>(data).size());
        return;
    }
    const std::string &message = std::get<std::string>(data);
    const QByteArray messageBytes(message.data(), static_cast<int>(message.size()));
    if (consumeImmediateTransferCancel(messageBytes))
        return;
    if (QThread::currentThread() != thread())
    {
        bool scheduleDrain = false;
        bool overflowed = false;
        {
            QMutexLocker locker(&m_fileTextIngressMutex);
            overflowed = m_fileTextIngress.size() >= kMaxFileTextIngressMessages ||
                         messageBytes.size() > kMaxFileTextIngressBytes - m_fileTextIngressBytes;
            if (!overflowed)
            {
                m_fileTextIngress.enqueue(messageBytes);
                m_fileTextIngressBytes += messageBytes.size();
                if (!m_fileTextIngressScheduled)
                {
                    m_fileTextIngressScheduled = true;
                    scheduleDrain = true;
                }
            }
        }

        if (overflowed)
        {
            if (m_fileTextIngressOverflowed.exchange(true))
                return;
            LOG_ERROR("CLI file-text ingress queue is full; closing the channel instead of dropping protocol messages");
            if (m_fileTextChannel)
                m_fileTextChannel->close();
            return;
        }
        if (scheduleDrain)
        {
            const QPointer<WebRtcCli> guard(this);
            m_callbackDispatcher->post([guard]() {
                if (guard)
                    guard->drainFileTextIngress();
            });
        }
        return;
    }

    processFileTextChannelMessage(messageBytes);
}


bool WebRtcCli::consumeImmediateTransferCancel(const QByteArray &data)
{
    const QByteArray cancelType = Constant::TYPE_FILE_TRANSFER_CANCEL.toUtf8();
    if (!data.contains(cancelType))
        return false;

    const QJsonObject object = JsonUtil::safeParseObject(data);
    if (JsonUtil::getString(object, Constant::KEY_MSGTYPE) !=
        Constant::TYPE_FILE_TRANSFER_CANCEL)
    {
        return false;
    }

    const QString transferId = JsonUtil::getString(object, Constant::KEY_TRANSFER_ID);
    markTransferCancelled(transferId);
    if (m_filePacketUtil)
        m_filePacketUtil->cancelTransfer(transferId);
    LOG_INFO("Received immediate transfer cancel: {}", transferId);
    return true;
}


void WebRtcCli::drainFileTextIngress()
{
    QQueue<QByteArray> messages;
    bool scheduleAgain = false;
    {
        QMutexLocker locker(&m_fileTextIngressMutex);
        while (!m_fileTextIngress.isEmpty() && messages.size() < kFileTextIngressBatchSize)
        {
            QByteArray message = m_fileTextIngress.dequeue();
            m_fileTextIngressBytes -= message.size();
            messages.enqueue(std::move(message));
        }
        scheduleAgain = !m_fileTextIngress.isEmpty();
        m_fileTextIngressScheduled = scheduleAgain;
    }

    while (!messages.isEmpty() && !m_shutdownStarted.load())
        processFileTextChannelMessage(messages.dequeue());

    if (scheduleAgain && !m_shutdownStarted.load())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->drainFileTextIngress();
        });
    }
}


void WebRtcCli::processFileTextChannelMessage(const QByteArray &messageBytes)
{
    LOG_TRACE("File text channel received text message, size={} bytes", messageBytes.size());

    QJsonParseError parseError{};
    QJsonDocument doc = QJsonDocument::fromJson(messageBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        LOG_ERROR("File text channel message parse error: {}", parseError.errorString());
        return;
    }

    const QJsonObject object = doc.object();
    if (JsonUtil::getString(object, Constant::KEY_MSGTYPE) == Constant::TYPE_FILE_TRANSFER_CANCEL)
    {
        const QString transferId = JsonUtil::getString(object, Constant::KEY_TRANSFER_ID);
        markTransferCancelled(transferId);
        if (m_filePacketUtil)
            m_filePacketUtil->cancelTransfer(transferId);
        LOG_INFO("Received transfer cancel: {}", transferId);
        return;
    }

    parseFileMsg(object);
}


void WebRtcCli::onFileTextChannelError(std::string error)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard, error = std::move(error)]() mutable {
            if (guard)
                guard->onFileTextChannelError(std::move(error));
        });
        return;
    }
    LOG_ERROR("File text channel error: {}", error);
    m_disconnectReason = QStringLiteral("file_text_channel_error");
    emit destroyCli();
}


void WebRtcCli::onFileTextChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onFileTextChannelClosed();
        });
        return;
    }
    LOG_INFO("File text channel closed");
    {
        QMutexLocker locker(&m_fileTextIngressMutex);
        m_fileTextIngress.clear();
        m_fileTextIngressBytes = 0;
        m_fileTextIngressScheduled = false;
        m_fileTextIngressOverflowed.store(false);
    }
    m_disconnectReason = QStringLiteral("file_text_channel_closed");
    emit destroyCli();
}
