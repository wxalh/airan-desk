#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/json/json_util.h"

#include <QDir>
#include <QDirIterator>
#include <limits>

namespace
{
constexpr qint64 kCancelledTransferRetentionMs = 10 * 60 * 1000;
constexpr int kMaxCancelledTransfers = 1024;
}
#include <QFileInfo>


bool WebRtcCli::isTransferCancelled(const QString &transferId) const
{
    if (transferId.isEmpty())
        return false;
    QMutexLocker locker(&m_transferMutex);
    return m_cancelledTransfers.contains(transferId);
}


void WebRtcCli::markTransferCancelled(const QString &transferId)
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


void WebRtcCli::sendTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes,
                                     int transferredFiles, int totalFiles, const QString &ctlPath, const QString &cliPath)
{
    if (transferId.isEmpty())
        return;
    noteSessionTransportProgress();

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_TRANSFER_PROGRESS)
                          .add(Constant::KEY_TRANSFER_ID, transferId)
                          .add(Constant::KEY_TRANSFER_BYTES, static_cast<double>(transferredBytes))
                          .add(Constant::KEY_TRANSFER_TOTAL_BYTES, static_cast<double>(totalBytes))
                          .add(Constant::KEY_TRANSFER_FILE_COUNT, transferredFiles)
                          .add(Constant::KEY_TRANSFER_TOTAL_FILES, totalFiles)
                          .build();
    if (!ctlPath.isEmpty())
        obj.insert(Constant::KEY_PATH_CTL, ctlPath);
    if (!cliPath.isEmpty())
        obj.insert(Constant::KEY_PATH_CLI, cliPath);
    sendFileTextChannelMessage(obj);
}


void WebRtcCli::sendTransferCancel(const QString &transferId)
{
    if (transferId.isEmpty())
        return;

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_TRANSFER_CANCEL)
                          .add(Constant::KEY_TRANSFER_ID, transferId)
                          .build();
    sendFileTextChannelMessage(obj);
}


qint64 WebRtcCli::collectDirectoryStats(const QString &path, int *fileCount,
                                        const QString &transferId) const
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
        if ((totalFiles & 0xff) == 0)
        {
            if (m_shutdownRequested.load() || m_shutdownStarted.load() ||
                isTransferCancelled(transferId))
                return -1;
        }
        QFileInfo fileInfo(it.next());
        const qint64 fileSize = qMax<qint64>(0, fileInfo.size());
        totalBytes = totalBytes <= (std::numeric_limits<qint64>::max)() - fileSize
                         ? totalBytes + fileSize
                         : (std::numeric_limits<qint64>::max)();
        if (totalFiles < (std::numeric_limits<int>::max)())
            ++totalFiles;
    }
    if (m_shutdownRequested.load() || m_shutdownStarted.load() ||
        isTransferCancelled(transferId))
        return -1;
    if (fileCount)
        *fileCount = totalFiles;
    return totalBytes;
}
