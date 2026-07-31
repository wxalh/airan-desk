#ifndef FILE_PACKET_UTIL_H
#define FILE_PACKET_UTIL_H

#include <QObject>
#include <QMutex>
#include <QDateTime>
#include <QUuid>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QDataStream>
#include <QThread>
#include <QJsonObject>
#include <QHash>
#include <memory>
#include <map>
#include <functional>
#include "common/constant.h"


constexpr quint64 FRAGMENT_SIZE = 8 * 1024; /* 8KB */
constexpr quint64 HEADER_SIZE = 32;         
constexpr quint64 PAYLOAD_SIZE = FRAGMENT_SIZE - HEADER_SIZE; 
constexpr quint64 MAX_REASONABLE_OFFSET = 100LL * 1024 * 1024 * 1024; /* 100GB */


struct ReassemblyBuffer {
    quint64 totalFragments = 0;
    quint64 nextFragmentIndex = 0;
    QString tempFilePath;  
    qint64 timestamp = 0; 
    QFile* tempFile = nullptr;  
    quint64 receivedBytes = 0;
};


class FilePacketUtil : public QObject
{
    Q_OBJECT

public:
    using ProgressCallback = std::function<void(qint64 sentBytes, qint64 totalBytes)>;
    using CancelCallback = std::function<bool()>;

    explicit FilePacketUtil(QObject *parent = nullptr);
    ~FilePacketUtil();

    
    static bool sendFileStream(const QString &filePath, const QJsonObject &header, std::shared_ptr<rtc::DataChannel> channel,
                               const ProgressCallback &progressCallback = ProgressCallback(),
                               const CancelCallback &cancelCallback = CancelCallback());

    
    static bool sendDataPacket(const QJsonObject &header, const QByteArray &payload, std::shared_ptr<rtc::DataChannel> channel,
                               const ProgressCallback &progressCallback = ProgressCallback(),
                               const CancelCallback &cancelCallback = CancelCallback());

    
    void processReceivedFragment(const rtc::binary &data, const QString &channelName);

    
    void processFileDataPacket(const QString &tempFilePath);

    
    void cancelTransfer(const QString &transferId);

    
signals:
    
    void fileDownloadCompleted(bool status, const QString &tempPath);

    
    void fileReceived(bool status, const QString &tempPath, const QString &errorMessage);

private:
    static bool waitForChannelBackpressure(const std::shared_ptr<rtc::DataChannel> &channel, const QString &filePath,
                                           const CancelCallback &cancelCallback);
    static bool sendPacketStream(QFile *file, const QByteArray &payload, const QJsonObject &header,
                                 std::shared_ptr<rtc::DataChannel> channel, const QString &logPath,
                                 const ProgressCallback &progressCallback, const CancelCallback &cancelCallback);
    void discardReassemblyLocked(const QString &messageId);
    void cleanupExpiredReassembliesLocked(qint64 nowMs);
    void cleanupExpiredCancelledTransfersLocked(qint64 nowMs);

    
    void reassembleFragment(const QString &messageId, quint64 fragmentIndex,
                           quint64 totalFragments, const rtc::binary &fragment);

    
    bool streamCopyFile(QFile &sourceFile, qint64 sourceOffset, const QString &targetPath, qint64 dataSize,
                        QString *errorMessage = nullptr);

    
    std::map<QString, ReassemblyBuffer> m_reassemblyBuffers;
    QHash<QString, qint64> m_cancelledTransfers;

    
    QMutex m_reassemblyMutex;
};

#endif /* FILE_PACKET_UTIL_H */
