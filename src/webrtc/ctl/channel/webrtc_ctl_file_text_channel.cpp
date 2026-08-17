#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QPointer>

namespace
{
constexpr int kMaxFileTextMessageBytes = 16 * 1024 * 1024;
constexpr qint64 kMaxFileTextIngressBytes = 64LL * 1024 * 1024;
constexpr int kMaxFileTextIngressMessages = 8192;
}


void WebRtcCtl::onFileTextChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onFileTextChannelOpen();
        });
        return;
    }
    const QString channelLabel = m_fileTextChannel ? QString::fromStdString(m_fileTextChannel->label()) : QString();
    LOG_INFO("File text channel opened: {}", channelLabel);
    m_fileTextIngressOverflowed.store(false);
    flushPendingFileTextMessages();
    emit fileTextChannelOpened();
}


void WebRtcCtl::onFileTextChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onFileTextChannelClosed();
        });
        return;
    }
    const QString channelLabel = m_fileTextChannel ? QString::fromStdString(m_fileTextChannel->label()) : QString();
    {
        QMutexLocker locker(&m_fileTextIngressMutex);
        m_fileTextIngress.clear();
        m_fileTextIngressBytes.store(0);
        m_fileTextIngressScheduled = false;
        m_fileTextIngressOverflowed.store(false);
    }
    LOG_INFO("File text channel closed: {}", channelLabel);
    requestSessionReconnect(tr("File control channel closed, reconnecting..."));
}


void WebRtcCtl::onFileTextChannelError(const std::string &error)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const std::string errorCopy = error;
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, errorCopy]() {
            if (guard)
                guard->onFileTextChannelError(errorCopy);
        });
        return;
    }
    LOG_ERROR("File text channel error: {}", error);
    requestSessionReconnect(tr("File control channel failed, reconnecting..."));
}


void WebRtcCtl::onFileTextChannelMessage(const rtc::message_variant &message)
{
    if (m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (!std::holds_alternative<std::string>(message))
    {
        LOG_WARN("File text channel received binary data, ignoring");
        return;
    }

    const std::string &data = std::get<std::string>(message);
    if (data.size() > static_cast<size_t>(kMaxFileTextMessageBytes))
    {
        LOG_WARN("Rejected oversized file text channel message: size={} bytes", data.size());
        return;
    }
    // The WebRTC callback owns `data` only for the duration of this call. The
    // ingress queue is drained asynchronously, so it must receive an owning
    // copy rather than a QByteArray view into the callback buffer.
    const QByteArray dataArr(data.data(), static_cast<int>(data.size()));
    if (consumeImmediateTransferCancel(dataArr))
        return;
    if (QThread::currentThread() != thread())
    {
        bool scheduleDrain = false;
        {
            QMutexLocker locker(&m_fileTextIngressMutex);
            const qint64 queuedBytes = m_fileTextIngressBytes.load();
            if (m_fileTextIngress.size() >= kMaxFileTextIngressMessages ||
                queuedBytes > kMaxFileTextIngressBytes - dataArr.size())
            {
                if (m_fileTextIngressOverflowed.exchange(true))
                    return;
                LOG_ERROR("File text ingress queue is full; closing file text channel to prevent silently dropping protocol messages: size={}, queuedBytes={}, queuedMessages={}",
                          dataArr.size(), queuedBytes, m_fileTextIngress.size());
                if (m_fileTextChannel)
                    m_fileTextChannel->close();
                return;
            }
            m_fileTextIngress.enqueue(dataArr);
            m_fileTextIngressBytes.fetch_add(dataArr.size());
            if (!m_fileTextIngressScheduled)
            {
                m_fileTextIngressScheduled = true;
                scheduleDrain = true;
            }
        }
        if (scheduleDrain)
        {
            const QPointer<WebRtcCtl> guard(this);
            m_callbackDispatcher->post([guard]() {
                if (guard)
                    guard->drainFileTextIngress();
            });
        }
        return;
    }

    processFileTextChannelMessage(dataArr);
}


bool WebRtcCtl::consumeImmediateTransferCancel(const QByteArray &data)
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


void WebRtcCtl::processFileTextChannelMessage(const QByteArray &dataArr)
{
    LOG_TRACE("File text channel received text message, size={} bytes", dataArr.size());

    const QJsonObject object = JsonUtil::safeParseObject(dataArr);
    if (!JsonUtil::isValidObject(object))
        return;

    const QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
    LOG_TRACE("Parsed file text channel message type={}, size={} bytes", msgType, dataArr.size());
    handleFileTextChannelObject(object);
}


void WebRtcCtl::drainFileTextIngress()
{
    constexpr int kMaxMessagesPerDrain = 256;
    QQueue<QByteArray> messages;
    bool scheduleAgain = false;
    {
        QMutexLocker locker(&m_fileTextIngressMutex);
        while (!m_fileTextIngress.isEmpty() && messages.size() < kMaxMessagesPerDrain)
        {
            QByteArray message = m_fileTextIngress.dequeue();
            m_fileTextIngressBytes.fetch_sub(message.size());
            messages.enqueue(std::move(message));
        }
        m_fileTextIngressScheduled = !m_fileTextIngress.isEmpty();
        scheduleAgain = m_fileTextIngressScheduled;
    }

    updateTerminalFlowControl();
    while (!messages.isEmpty())
        processFileTextChannelMessage(messages.dequeue());

    if (scheduleAgain)
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->drainFileTextIngress();
        });
    }
}


void WebRtcCtl::handleFileTextChannelObject(const QJsonObject &object)
{
    const QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
    if (handleTerminalTextChannelObject(object, msgType))
        return;
    if (handleFileTransferTextChannelObject(object, msgType))
        return;
    emit recvGetFileList(object);
}
