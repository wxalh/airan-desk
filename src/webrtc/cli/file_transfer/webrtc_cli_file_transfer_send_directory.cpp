#include "webrtc/cli/webrtc_cli.h"

#include <limits>

#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include <functional>
#include <memory>
#include <utility>

namespace
{
constexpr int kMaxDirectoryTransferEntries = 100000;

struct CliDirectorySendState
{
    QString cliPath;
    QString ctlPath;
    QString transferId;
    QVector<CliDirectoryFileEntry> files;
    int nextFileIndex{0};
    qint64 totalBytes{0};
    qint64 transferredBytes{0};
    int totalFiles{0};
    int fileCount{0};
    bool hasErrors{false};
    QVector<QJsonObject> metadataHeaders;
    int nextMetadataIndex{0};
};

class CliDirectoryStatsTask final : public QRunnable
{
public:
    CliDirectoryStatsTask(QString path,
                          std::function<bool()> cancelled,
                          std::function<void(qint64, int)> completed)
        : m_path(std::move(path)),
          m_cancelled(std::move(cancelled)),
          m_completed(std::move(completed))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        qint64 totalBytes = 0;
        int totalFiles = 0;
        bool cancelled = false;
        try
        {
            QDirIterator it(m_path,
                            QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
            {
                if ((totalFiles & 0xff) == 0 && m_cancelled && m_cancelled())
                {
                    cancelled = true;
                    break;
                }
                const qint64 size = qMax<qint64>(0, QFileInfo(it.next()).size());
                if (totalFiles >= kMaxDirectoryTransferEntries)
                {
                    cancelled = true;
                    break;
                }
                totalBytes = totalBytes <= (std::numeric_limits<qint64>::max)() - size
                                 ? totalBytes + size
                                 : (std::numeric_limits<qint64>::max)();
                if (totalFiles < (std::numeric_limits<int>::max)())
                    ++totalFiles;
            }
            if (!cancelled && m_cancelled && m_cancelled())
                cancelled = true;
        }
        catch (...)
        {
            cancelled = true;
        }
        if (m_completed)
            m_completed(cancelled ? -1 : totalBytes, cancelled ? 0 : totalFiles);
    }

private:
    QString m_path;
    std::function<bool()> m_cancelled;
    std::function<void(qint64, int)> m_completed;
};

class CliDirectoryMetadataTask final : public QRunnable
{
public:
    CliDirectoryMetadataTask(QString rootPath,
                             QString remotePath,
                             QJsonObject startHeader,
                             std::function<bool()> cancelled,
                             std::function<void(QVector<QJsonObject>, QVector<CliDirectoryFileEntry>, bool)> completed)
        : m_rootPath(std::move(rootPath)),
          m_remotePath(std::move(remotePath)),
          m_startHeader(std::move(startHeader)),
          m_cancelled(std::move(cancelled)),
          m_completed(std::move(completed))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QVector<QJsonObject> headers;
        QVector<CliDirectoryFileEntry> fileEntries;
        headers.append(m_startHeader);
        bool cancelled = false;
        try
        {
            const QDir root(m_rootPath);
            QDirIterator it(m_rootPath,
                            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            int directoryCount = 0;
            while (it.hasNext())
            {
                if ((directoryCount & 0xff) == 0 && m_cancelled && m_cancelled())
                {
                    cancelled = true;
                    break;
                }
                const QFileInfo dirInfo(it.next());
                if (directoryCount >= kMaxDirectoryTransferEntries)
                {
                    cancelled = true;
                    break;
                }
                if (dirInfo.isSymLink())
                    continue;
                const QString relativePath = root.relativeFilePath(dirInfo.absoluteFilePath());
                const QString targetPath = QDir::cleanPath(m_remotePath + "/" + relativePath);
                headers.append(JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                                   .add(Constant::KEY_PATH_CLI, dirInfo.absoluteFilePath())
                                   .add(Constant::KEY_PATH_CTL, targetPath)
                                   .add(Constant::KEY_TRANSFER_ID,
                                        JsonUtil::getString(m_startHeader, Constant::KEY_TRANSFER_ID))
                                   .add("isDirectory", true)
                                   .add("directoryStart", true)
                                   .build());
                ++directoryCount;
            }
            if (!cancelled && m_cancelled && m_cancelled())
                cancelled = true;
            if (!cancelled)
            {
                QDir root(m_rootPath);
                QDirIterator files(m_rootPath,
                                   QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                   QDirIterator::Subdirectories);
                int fileCount = 0;
                while (files.hasNext())
                {
                    if ((fileCount & 0xff) == 0 && m_cancelled && m_cancelled())
                    {
                        cancelled = true;
                        break;
                    }
                    const QFileInfo fileInfo(files.next());
                    if (!fileInfo.isFile())
                        continue;
                    if (fileCount >= kMaxDirectoryTransferEntries)
                    {
                        cancelled = true;
                        break;
                    }
                    const QString relativePath = root.relativeFilePath(fileInfo.absoluteFilePath());
                    fileEntries.append({fileInfo.absoluteFilePath(),
                                        QDir::cleanPath(m_remotePath + "/" + relativePath),
                                        qMax<qint64>(0, fileInfo.size())});
                    ++fileCount;
                }
            }
        }
        catch (...)
        {
            cancelled = true;
        }
        if (m_completed)
            m_completed(std::move(headers), std::move(fileEntries), !cancelled);
    }

private:
    QString m_rootPath;
    QString m_remotePath;
    QJsonObject m_startHeader;
    std::function<bool()> m_cancelled;
    std::function<void(QVector<QJsonObject>, QVector<CliDirectoryFileEntry>, bool)> m_completed;
};
}


bool WebRtcCli::sendFileMetadataPacket(const QJsonObject &header, const QString &transferId)
{
    if (!m_fileChannel || !m_fileChannel->isOpen())
    {
        LOG_ERROR("File channel not available for metadata packet");
        return false;
    }
    try
    {
        const auto cancelCallback = [this, transferId]() {
            return m_shutdownRequested.load() || m_shutdownStarted.load() ||
                   isTransferCancelled(transferId);
        };
        return FilePacketUtil::sendDataPacket(header, QByteArray(), m_fileChannel, FilePacketUtil::ProgressCallback(), cancelCallback);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception during metadata packet send: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Exception during metadata packet send: unknown error");
    }
    return false;
}


void WebRtcCli::sendFileMetadataPacketAsync(const QJsonObject &header,
                                            const QString &transferId,
                                            const std::function<void(bool)> &completion)
{
    if (!completion)
        return;
    if (!m_fileChannel || !m_fileChannel->isOpen())
    {
        completion(false);
        return;
    }
    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const auto cancelCallback = [guard, callbackLifetime, transferId]() {
        auto permit = callbackLifetime->tryEnter();
        if (!permit)
            return true;
        return !guard || guard->m_shutdownRequested.load() || guard->m_shutdownStarted.load() ||
               guard->isTransferCancelled(transferId);
    };
    FilePacketUtil::sendDataPacketAsync(
        header,
        QByteArray(),
        m_fileChannel,
        FilePacketUtil::ProgressCallback(),
        cancelCallback,
        [guard, callbackLifetime, completion](bool success, const QString &) {
            auto permit = callbackLifetime->tryEnter();
            if (!permit || !guard)
                return;
            if (guard && guard->m_callbackDispatcher)
                guard->m_callbackDispatcher->post([guard, callbackLifetime, completion, success]() {
                auto permit = callbackLifetime->tryEnter();
                if (permit && guard)
                    completion(success);
                });
        });
}


void WebRtcCli::sendDirectory(const QString &cliPath, const QString &ctlPath, const QString &transferId)
{
    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const auto cancelled = [guard, callbackLifetime, transferId]() {
        auto permit = callbackLifetime->tryEnter();
        if (!permit)
            return true;
        return !guard || guard->m_shutdownRequested.load() || guard->m_shutdownStarted.load() ||
               guard->isTransferCancelled(transferId);
    };
    QThreadPool::globalInstance()->start(new CliDirectoryStatsTask(
        cliPath,
        cancelled,
        [guard, callbackLifetime, cliPath, ctlPath, transferId](qint64 totalBytes, int totalFiles) {
            auto permit = callbackLifetime->tryEnter();
            if (!permit || !guard)
                return;
            if (guard && guard->m_callbackDispatcher)
                guard->m_callbackDispatcher->post([guard, callbackLifetime, cliPath, ctlPath, transferId, totalBytes, totalFiles]() {
                                          auto permit = callbackLifetime->tryEnter();
                                          if (permit && guard)
                                              guard->continueSendDirectory(cliPath,
                                                                           ctlPath,
                                                                           transferId,
                                                                           totalBytes,
                                                                           totalFiles);
                                      });
        }));
}


void WebRtcCli::continueSendDirectory(const QString &cliPath, const QString &ctlPath, const QString &transferId,
                                      qint64 totalBytes, int totalFiles)
{
    if (totalBytes < 0)
    {
        LOG_INFO("Download directory statistics cancelled: {}", cliPath);
        processNextDownload();
        return;
    }

    const QJsonObject dirStartHeader = JsonUtil::createObject()
                                     .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                                     .add(Constant::KEY_PATH_CLI, cliPath)
                                     .add(Constant::KEY_PATH_CTL, ctlPath)
                                     .add(Constant::KEY_TRANSFER_ID, transferId)
                                     .add(Constant::KEY_TRANSFER_TOTAL_BYTES, static_cast<double>(totalBytes))
                                     .add(Constant::KEY_TRANSFER_TOTAL_FILES, totalFiles)
                                     .add("isDirectory", true)
                                     .add("directoryStart", true)
                                     .build();

    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const auto cancelled = [guard, callbackLifetime, transferId]() {
        auto permit = callbackLifetime->tryEnter();
        if (!permit)
            return true;
        return !guard || guard->m_shutdownRequested.load() || guard->m_shutdownStarted.load() ||
               guard->isTransferCancelled(transferId);
    };
    QThreadPool::globalInstance()->start(new CliDirectoryMetadataTask(
        cliPath,
        ctlPath,
        dirStartHeader,
        cancelled,
        [guard, callbackLifetime, cliPath, ctlPath, transferId, totalBytes, totalFiles](QVector<QJsonObject> headers,
                                                                                           QVector<CliDirectoryFileEntry> fileEntries,
                                                                                           bool success) {
            auto permit = callbackLifetime->tryEnter();
            if (!permit || !guard)
                return;
            if (guard && guard->m_callbackDispatcher)
                guard->m_callbackDispatcher->post([guard, callbackLifetime, cliPath, ctlPath, transferId, totalBytes, totalFiles,
                                                    headers = std::move(headers), fileEntries = std::move(fileEntries),
                                                    success]() mutable {
                                          auto permit = callbackLifetime->tryEnter();
                                          if (!permit || !guard)
                                              return;
                                          if (!success)
                                          {
                                              guard->processNextDownload();
                                              return;
                                          }
                                          guard->continueSendDirectoryWithMetadata(cliPath,
                                                                                    ctlPath,
                                                                                    transferId,
                                                                                    totalBytes,
                                                                                    totalFiles,
                                                                                    std::move(headers),
                                                                                    std::move(fileEntries));
                                      });
        }));
}

void WebRtcCli::continueSendDirectoryWithMetadata(const QString &cliPath, const QString &ctlPath,
                                                  const QString &transferId, qint64 totalBytes, int totalFiles,
                                                  QVector<QJsonObject> metadataHeaders,
                                                  QVector<CliDirectoryFileEntry> fileEntries)
{
    if (totalBytes < 0)
    {
        LOG_INFO("Download directory statistics cancelled: {}", cliPath);
        processNextDownload();
        return;
    }
    sendTransferProgress(transferId, 0, totalBytes, 0, totalFiles, ctlPath, cliPath);
    auto state = std::make_shared<CliDirectorySendState>();
    state->cliPath = cliPath;
    state->ctlPath = ctlPath;
    state->transferId = transferId;
    state->totalBytes = totalBytes;
    state->totalFiles = totalFiles;
    state->metadataHeaders = std::move(metadataHeaders);
    state->files = std::move(fileEntries);

    const auto next = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weakNext(next);
    *next = [this, state, weakNext]() {
        if (isTransferCancelled(state->transferId) || m_shutdownRequested.load())
        {
            LOG_INFO("Download directory cancelled: {}", state->cliPath);
            processNextDownload();
            return;
        }
        if (state->nextFileIndex < state->files.size())
        {
            const CliDirectoryFileEntry fileEntry = state->files.at(state->nextFileIndex++);
            const auto nextStep = weakNext.lock();
            if (!nextStep)
                return;
            sendSingleFileAsync(fileEntry.cliPath,
                                fileEntry.ctlPath,
                                state->transferId,
                                state->transferredBytes,
                                state->totalBytes,
                                state->fileCount + 1,
                                state->totalFiles,
                                [this, state, nextStep, fileSize = fileEntry.size](bool success) {
                                    if (success)
                                    {
                                        ++state->fileCount;
                                        state->transferredBytes = state->transferredBytes <=
                                                                          (std::numeric_limits<qint64>::max)() - fileSize
                                                                      ? state->transferredBytes + fileSize
                                                                      : (std::numeric_limits<qint64>::max)();
                                    }
                                    else if (isTransferCancelled(state->transferId))
                                    {
                                        processNextDownload();
                                        return;
                                    }
                                    else
                                    {
                                        state->hasErrors = true;
                                    }
                                    if (m_callbackDispatcher)
                                        m_callbackDispatcher->post([nextStep]() { (*nextStep)(); });
                                },
                                fileEntry.size);
            return;
        }

        const QJsonObject dirEndHeader = JsonUtil::createObject()
                                             .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                                             .add(Constant::KEY_PATH_CLI, state->cliPath)
                                             .add(Constant::KEY_PATH_CTL, state->ctlPath)
                                             .add(Constant::KEY_TRANSFER_ID, state->transferId)
                                             .add(Constant::KEY_TRANSFER_TOTAL_BYTES, static_cast<double>(state->totalBytes))
                                             .add(Constant::KEY_TRANSFER_TOTAL_FILES, state->totalFiles)
                                             .add("isDirectory", true)
                                             .add("directoryEnd", true)
                                             .add("fileCount", state->fileCount)
                                             .add("status", !state->hasErrors)
                                             .build();
        sendFileMetadataPacketAsync(dirEndHeader, state->transferId,
                                    [this, state](bool completionSent) {
                                        if (!completionSent)
                                            state->hasErrors = true;
                                        LOG_INFO("Sent directory: {} -> {} ({} files, hasErrors={})",
                                                 state->cliPath,
                                                 state->ctlPath,
                                                 state->fileCount,
                                                 state->hasErrors);
                                        sendTransferProgress(state->transferId,
                                                             state->totalBytes,
                                                             state->totalBytes,
                                                             state->totalFiles,
                                                             state->totalFiles,
                                                             state->ctlPath,
                                                             state->cliPath);
                                        processNextDownload();
                                    });
    };
    const auto sendMetadata = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weakSendMetadata(sendMetadata);
    *sendMetadata = [this, state, next, weakSendMetadata]() {
        if (isTransferCancelled(state->transferId) || m_shutdownRequested.load())
        {
            processNextDownload();
            return;
        }
        if (state->nextMetadataIndex < state->metadataHeaders.size())
        {
            const QJsonObject header = state->metadataHeaders.at(state->nextMetadataIndex++);
            const auto sendMetadataStep = weakSendMetadata.lock();
            if (!sendMetadataStep)
                return;
            sendFileMetadataPacketAsync(header,
                                        state->transferId,
                                        [this, state, next, sendMetadataStep](bool success) {
                                            state->hasErrors = state->hasErrors || !success;
                                            if (m_callbackDispatcher)
                                                m_callbackDispatcher->post([sendMetadataStep]() { (*sendMetadataStep)(); });
                                        });
            return;
        }
        (*next)();
    };
    (*sendMetadata)();
}
