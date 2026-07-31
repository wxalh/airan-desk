#include "util/file/file_packet_util.h"

namespace
{
constexpr qint64 kCancelledTransferRetentionMs = 10 * 60 * 1000;
constexpr int kMaxCancelledTransfers = 1024;
}


void FilePacketUtil::cleanupExpiredCancelledTransfersLocked(qint64 nowMs)
{
    for (auto it = m_cancelledTransfers.begin(); it != m_cancelledTransfers.end();)
    {
        if (nowMs - it.value() > kCancelledTransferRetentionMs)
            it = m_cancelledTransfers.erase(it);
        else
            ++it;
    }

    while (m_cancelledTransfers.size() >= kMaxCancelledTransfers)
    {
        auto oldest = m_cancelledTransfers.begin();
        for (auto it = m_cancelledTransfers.begin(); it != m_cancelledTransfers.end(); ++it)
        {
            if (it.value() < oldest.value())
                oldest = it;
        }
        m_cancelledTransfers.erase(oldest);
    }
}


void FilePacketUtil::cancelTransfer(const QString &transferId)
{
    if (transferId.isEmpty())
        return;

    QMutexLocker locker(&m_reassemblyMutex);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    cleanupExpiredCancelledTransfersLocked(nowMs);
    m_cancelledTransfers.insert(transferId, nowMs);
}
