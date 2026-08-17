#include "util/file/file_packet_util.h"

FilePacketUtil::FilePacketUtil(QObject *parent)
    : QObject(parent)
{
}

FilePacketUtil::~FilePacketUtil()
{
    clearPendingReassemblies();
}

void FilePacketUtil::clearPendingReassemblies()
{
    QMutexLocker locker(&m_reassemblyMutex);

    for (auto& pair : m_reassemblyBuffers) {
        if (pair.second.tempFile) {
            pair.second.tempFile->close();
            delete pair.second.tempFile;
        }
        if (!pair.second.tempFilePath.isEmpty()) {
            QFile::remove(pair.second.tempFilePath);
        }
    }

    m_reassemblyBuffers.clear();
}
