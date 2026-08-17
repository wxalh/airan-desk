#include "util/file/file_packet_util.h"
#include "util/text/convert_util.h"

#include <QTemporaryFile>

namespace
{
constexpr qint64 kReassemblyTimeoutMs = 10 * 60 * 1000;
constexpr size_t kMaxActiveReassemblies = 64;
}


void FilePacketUtil::discardReassemblyLocked(const QString &messageId)
{
    const auto it = m_reassemblyBuffers.find(messageId);
    if (it == m_reassemblyBuffers.end())
        return;

    ReassemblyBuffer &buffer = it->second;
    if (buffer.tempFile)
    {
        buffer.tempFile->close();
        delete buffer.tempFile;
        buffer.tempFile = nullptr;
    }
    if (!buffer.tempFilePath.isEmpty())
        QFile::remove(buffer.tempFilePath);
    m_reassemblyBuffers.erase(it);
}


void FilePacketUtil::cleanupExpiredReassembliesLocked(qint64 nowMs)
{
    for (auto it = m_reassemblyBuffers.begin(); it != m_reassemblyBuffers.end();)
    {
        ReassemblyBuffer &buffer = it->second;
        if (buffer.timestamp > 0 && nowMs - buffer.timestamp > kReassemblyTimeoutMs)
        {
            LOG_WARN("Reassembly expired, remove temp file: {}", buffer.tempFilePath);
            const QString messageId = it->first;
            ++it;
            discardReassemblyLocked(messageId);
        }
        else
        {
            ++it;
        }
    }
}


void FilePacketUtil::reassembleFragment(const QString &messageId, quint64 fragmentIndex,
                                        quint64 totalFragments, const rtc::binary &fragment)
{
    QString completedTempFilePath;
    {
        QMutexLocker locker(&m_reassemblyMutex);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        cleanupExpiredReassembliesLocked(nowMs);

        if (totalFragments == 0 || fragmentIndex >= totalFragments)
        {
            LOG_ERROR("Invalid fragment parameters: index={}, total={}", fragmentIndex, totalFragments);
            return;
        }

        auto bufferIt = m_reassemblyBuffers.find(messageId);
        if (bufferIt == m_reassemblyBuffers.end())
        {
            if (m_reassemblyBuffers.size() >= kMaxActiveReassemblies)
            {
                LOG_ERROR("Too many active file reassemblies; dropping message {}", messageId);
                return;
            }
            bufferIt = m_reassemblyBuffers.emplace(messageId, ReassemblyBuffer{}).first;
        }
        ReassemblyBuffer &buffer = bufferIt->second;

        if (buffer.totalFragments == 0)
        {
            buffer.totalFragments = totalFragments;
            buffer.timestamp = nowMs;

            QString safeMessageId = QString(messageId).replace("/", "_").replace("\\", "_");
            auto *tempFile = new QTemporaryFile(
                QDir::temp().filePath(QStringLiteral("airan-file-%1-XXXXXX.tmp").arg(safeMessageId)));
            tempFile->setAutoRemove(false);
            buffer.tempFile = tempFile;
            if (!buffer.tempFile->open(QIODevice::WriteOnly))
            {
                LOG_ERROR("Failed to create unique temp file for reassembly: {}", tempFile->errorString());
                delete buffer.tempFile;
                buffer.tempFile = nullptr;
                m_reassemblyBuffers.erase(messageId);
                return;
            }
            buffer.tempFilePath = tempFile->fileName();

            LOG_DEBUG("Created reassembly temp file: {}", buffer.tempFilePath);
        }
        else if (buffer.totalFragments != totalFragments)
        {
            LOG_ERROR("Fragment count changed during reassembly: message={}, expected={}, got={}",
                      messageId, buffer.totalFragments, totalFragments);
            discardReassemblyLocked(messageId);
            return;
        }

        if (!buffer.tempFile)
        {
            LOG_ERROR("Temp file not available for fragment reassembly");
            discardReassemblyLocked(messageId);
            return;
        }

        if (fragmentIndex < buffer.nextFragmentIndex)
        {
            buffer.timestamp = nowMs;
            LOG_TRACE("Ignoring duplicate fragment {}/{} for {}", fragmentIndex + 1, totalFragments, messageId);
            return;
        }

        if (fragmentIndex != buffer.nextFragmentIndex)
        {
            LOG_ERROR("Out-of-order file fragment: message={}, expected={}, got={}",
                      messageId, buffer.nextFragmentIndex, fragmentIndex);
            discardReassemblyLocked(messageId);
            return;
        }

        if (fragmentIndex + 1 < totalFragments && fragment.size() != PAYLOAD_SIZE)
        {
            LOG_ERROR("Short non-final file fragment: message={}, index={}, size={}",
                      messageId, fragmentIndex, fragment.size());
            discardReassemblyLocked(messageId);
            return;
        }

        const quint64 offset = buffer.receivedBytes;
        if (offset > MAX_REASONABLE_OFFSET || fragment.size() > MAX_REASONABLE_OFFSET - offset)
        {
            LOG_ERROR("Invalid fragment offset calculated: {} (fragmentIndex: {}, PAYLOAD_SIZE: {})",
                      offset, fragmentIndex, PAYLOAD_SIZE);
            discardReassemblyLocked(messageId);
            return;
        }

        const qint64 written = buffer.tempFile->write(reinterpret_cast<const char *>(fragment.data()), fragment.size());
        if (written != static_cast<qint64>(fragment.size()))
        {
            LOG_ERROR("Failed to write fragment to temp file: {} (wanted: {}, written: {})",
                      buffer.tempFilePath, ConvertUtil::formatFileSize(fragment.size()), ConvertUtil::formatFileSize(written));
            discardReassemblyLocked(messageId);
            return;
        }

        ++buffer.nextFragmentIndex;
        buffer.receivedBytes += static_cast<quint64>(written);
        buffer.timestamp = nowMs;

        if (fragmentIndex % 1024 == 0 || fragmentIndex + 1 == totalFragments)
        {
            LOG_DEBUG("Fragment {}/{} written to temp file at offset {} ({})",
                      fragmentIndex + 1, totalFragments, offset, ConvertUtil::formatFileSize(fragment.size()));
        }

        const bool complete = (buffer.nextFragmentIndex == totalFragments);
        if (complete)
        {
            LOG_DEBUG("Fragment reassembly complete, temp file: {}", buffer.tempFilePath);

            buffer.tempFile->close();
            delete buffer.tempFile;
            buffer.tempFile = nullptr;

            const qint64 expectedSize = static_cast<qint64>(buffer.receivedBytes);
            QFileInfo tempInfo(buffer.tempFilePath);
            if (tempInfo.exists() && expectedSize >= 0 && tempInfo.size() != expectedSize)
            {
                LOG_ERROR("Reassembled temp file size mismatch: {} got={}, expected={}",
                          buffer.tempFilePath, tempInfo.size(), expectedSize);
                QFile::remove(buffer.tempFilePath);
            }
            else
            {
                completedTempFilePath = buffer.tempFilePath;
            }

            m_reassemblyBuffers.erase(messageId);
        }
    }

    
    if (!completedTempFilePath.isEmpty())
    {
        if (messageId.contains("file"))
            processFileDataPacket(completedTempFilePath);
        QFile::remove(completedTempFilePath);
    }
}
