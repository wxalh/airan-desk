#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"

#include <QDir>
#include <QDirIterator>

namespace
{
constexpr qint64 kCancelledTransferRetentionMs = 10 * 60 * 1000;
constexpr int kMaxCancelledTransfers = 1024;
}
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include <limits>


bool WebRtcCtl::isTransferCancelled(const QString &transferId) const
{
    if (transferId.isEmpty())
        return false;
    QMutexLocker locker(&m_transferMutex);
    return m_cancelledTransfers.contains(transferId);
}


void WebRtcCtl::markTransferCancelled(const QString &transferId)
{
    if (transferId.isEmpty())
        return;
    QMutexLocker locker(&m_transferMutex);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_cancelledTransfers.begin(); it != m_cancelledTransfers.end();)
    {
        if (nowMs - it.value() > kCancelledTransferRetentionMs)
            it = m_cancelledTransfers.erase(it);
        else
            ++it;
    }
    if (!m_cancelledTransfers.contains(transferId) && m_cancelledTransfers.size() >= kMaxCancelledTransfers)
    {
        auto oldest = m_cancelledTransfers.begin();
        for (auto it = m_cancelledTransfers.begin(); it != m_cancelledTransfers.end(); ++it)
            if (it.value() < oldest.value())
                oldest = it;
        m_cancelledTransfers.erase(oldest);
    }
    m_cancelledTransfers.insert(transferId, nowMs);
}


void WebRtcCtl::sendTransferCancel(const QString &transferId)
{
    if (transferId.isEmpty() || !m_fileTextChannel || !m_fileTextChannel->isOpen())
        return;

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_TRANSFER_CANCEL)
                          .add(Constant::KEY_TRANSFER_ID, transferId)
                          .build();
    try
    {
        m_fileTextChannel->send(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
        noteSessionOutboundActivity();
    }
    catch (const std::exception &e)
    {
        LOG_WARN("Failed to send transfer cancel: {}", e.what());
    }
}


void WebRtcCtl::cancelFileTransfer(const QString &transferId)
{
    markTransferCancelled(transferId);
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this,
                                  "cancelFileTransfer",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, transferId));
        return;
    }
    if (m_filePacketUtil)
        m_filePacketUtil->cancelTransfer(transferId);
    sendTransferCancel(transferId);
    LOG_INFO("Cancel requested for file transfer: {}", transferId);
}


void WebRtcCtl::emitTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes,
                                     int transferredFiles, int totalFiles)
{
    if (transferId.isEmpty())
        return;
    noteSessionTransportProgress();
    emit fileTransferProgress(transferId, transferredBytes, totalBytes, transferredFiles, totalFiles);
}


qint64 WebRtcCtl::collectDirectoryStats(const QString &path, int *fileCount) const
{
    if (fileCount)
        *fileCount = 0;

    QFileInfo info(path);
    if (!info.exists())
        return 0;

    if (info.isFile())
    {
        if (fileCount)
            *fileCount = 1;
        return info.size();
    }

    qint64 totalBytes = 0;
    int totalFiles = 0;
    QDirIterator it(path,
                    QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        QFileInfo fileInfo(it.next());
        const qint64 fileSize = qMax<qint64>(0, fileInfo.size());
        totalBytes = totalBytes <= (std::numeric_limits<qint64>::max)() - fileSize
                         ? totalBytes + fileSize
                         : (std::numeric_limits<qint64>::max)();
        if (totalFiles < (std::numeric_limits<int>::max)())
            ++totalFiles;
    }
    if (fileCount)
        *fileCount = totalFiles;
    return totalBytes;
}
