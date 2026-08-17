#include "webrtc/ctl/webrtc_ctl.h"

#include <limits>

#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include <functional>
#include <memory>
#include <utility>

namespace
{
constexpr int kMaxDirectoryTransferEntries = 100000;

struct CtlDirectorySendState
{
    QString ctlPath;
    QString cliPath;
    QString transferId;
    QDir root;
    std::unique_ptr<QDirIterator> files;
    qint64 totalBytes{0};
    qint64 transferredBytes{0};
    int totalFiles{0};
    int fileCount{0};
    bool hasErrors{false};
    QVector<QJsonObject> metadataHeaders;
    int nextMetadataIndex{0};
};

class CtlDirectoryStatsTask final : public QRunnable
{
public:
    CtlDirectoryStatsTask(QString path,
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
                if (totalFiles >= kMaxDirectoryTransferEntries)
                {
                    cancelled = true;
                    break;
                }
                const qint64 size = qMax<qint64>(0, QFileInfo(it.next()).size());
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

class CtlDirectoryMetadataTask final : public QRunnable
{
public:
    CtlDirectoryMetadataTask(QString rootPath,
                             QString remotePath,
                             QJsonObject startHeader,
                             std::function<bool()> cancelled,
                             std::function<void(QVector<QJsonObject>, bool)> completed)
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
                if (directoryCount >= kMaxDirectoryTransferEntries)
                {
                    cancelled = true;
                    break;
                }
                const QFileInfo dirInfo(it.next());
                if (dirInfo.isSymLink())
                    continue;
                const QString relativePath = root.relativeFilePath(dirInfo.absoluteFilePath());
                const QString targetPath = QDir::cleanPath(m_remotePath + "/" + relativePath);
                headers.append(JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                   .add(Constant::KEY_PATH_CTL, dirInfo.absoluteFilePath())
                                   .add(Constant::KEY_PATH_CLI, targetPath)
                                   .add(Constant::KEY_TRANSFER_ID,
                                        JsonUtil::getString(m_startHeader, Constant::KEY_TRANSFER_ID))
                                   .add("isDirectory", true)
                                   .add("directoryStart", true)
                                   .build());
                ++directoryCount;
            }
            if (!cancelled && m_cancelled && m_cancelled())
                cancelled = true;
        }
        catch (...)
        {
            cancelled = true;
        }
        if (m_completed)
            m_completed(std::move(headers), !cancelled);
    }

private:
    QString m_rootPath;
    QString m_remotePath;
    QJsonObject m_startHeader;
    std::function<bool()> m_cancelled;
    std::function<void(QVector<QJsonObject>, bool)> m_completed;
};
}


bool WebRtcCtl::sendFileMetadataPacket(const QJsonObject &header, const QString &transferId)
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


void WebRtcCtl::sendFileMetadataPacketAsync(const QJsonObject &header,
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
    const QPointer<WebRtcCtl> guard(this);
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


void WebRtcCtl::uploadDirectory(const QString &ctlPath, const QString &cliPath, const QString &transferId)
{
    QDir dir(ctlPath);
    if (!dir.exists())
    {
        LOG_ERROR("Directory does not exist: {}", ctlPath);
        emit recvUploadFileRes(false, cliPath, tr("Source directory does not exist."));
        processNextUpload();
        return;
    }

    const QPointer<WebRtcCtl> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const auto cancelled = [guard, callbackLifetime, transferId]() {
        auto permit = callbackLifetime->tryEnter();
        if (!permit)
            return true;
        return !guard || guard->m_shutdownRequested.load() || guard->m_shutdownStarted.load() ||
               guard->isTransferCancelled(transferId);
    };
    QThreadPool::globalInstance()->start(new CtlDirectoryStatsTask(
        ctlPath,
        cancelled,
        [guard, callbackLifetime, ctlPath, cliPath, transferId](qint64 totalBytes, int totalFiles) {
            auto permit = callbackLifetime->tryEnter();
            if (!permit || !guard)
                return;
            if (guard && guard->m_callbackDispatcher)
                guard->m_callbackDispatcher->post([guard, callbackLifetime, ctlPath, cliPath, transferId,
                                                    totalBytes, totalFiles]() {
                    auto permit = callbackLifetime->tryEnter();
                    if (permit && guard)
                        guard->continueUploadDirectory(ctlPath,
                                                       cliPath,
                                                       transferId,
                                                       totalBytes,
                                                       totalFiles);
                });
        }));
}


void WebRtcCtl::continueUploadDirectory(const QString &ctlPath, const QString &cliPath, const QString &transferId,
                                         qint64 totalBytes, int totalFiles)
{
    if (totalBytes < 0)
    {
        LOG_INFO("Upload directory statistics cancelled: {}", ctlPath);
        processNextUpload();
        return;
    }
    const QJsonObject dirStartHeader = JsonUtil::createObject()
                                     .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                     .add(Constant::KEY_PATH_CTL, ctlPath)
                                     .add(Constant::KEY_PATH_CLI, cliPath)
                                     .add(Constant::KEY_TRANSFER_ID, transferId)
                                     .add(Constant::KEY_TRANSFER_TOTAL_BYTES, static_cast<double>(totalBytes))
                                     .add(Constant::KEY_TRANSFER_TOTAL_FILES, totalFiles)
                                     .add("isDirectory", true)
                                     .add("directoryStart", true)
                                     .build();
    const QPointer<WebRtcCtl> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const auto cancelled = [guard, callbackLifetime, transferId]() {
        auto permit = callbackLifetime->tryEnter();
        if (!permit)
            return true;
        return !guard || guard->m_shutdownRequested.load() || guard->m_shutdownStarted.load() ||
               guard->isTransferCancelled(transferId);
    };
    QThreadPool::globalInstance()->start(new CtlDirectoryMetadataTask(
        ctlPath,
        cliPath,
        dirStartHeader,
        cancelled,
        [guard, callbackLifetime, ctlPath, cliPath, transferId, totalBytes, totalFiles](QVector<QJsonObject> headers, bool success) {
            auto permit = callbackLifetime->tryEnter();
            if (!permit || !guard)
                return;
            if (guard && guard->m_callbackDispatcher)
                guard->m_callbackDispatcher->post([guard, callbackLifetime, ctlPath, cliPath, transferId,
                                                    totalBytes, totalFiles, headers = std::move(headers), success]() mutable {
                    auto permit = callbackLifetime->tryEnter();
                    if (!permit || !guard)
                        return;
                    if (!success)
                    {
                        guard->processNextUpload();
                        return;
                    }
                    guard->continueUploadDirectoryWithMetadata(ctlPath,
                                                                cliPath,
                                                                transferId,
                                                                totalBytes,
                                                                totalFiles,
                                                                std::move(headers));
                });
        }));
}

void WebRtcCtl::continueUploadDirectoryWithMetadata(const QString &ctlPath, const QString &cliPath,
                                                    const QString &transferId, qint64 totalBytes, int totalFiles,
                                                    QVector<QJsonObject> metadataHeaders)
{
    const QDir dir(ctlPath);
    if (totalBytes < 0)
    {
        LOG_INFO("Upload directory statistics cancelled: {}", ctlPath);
        processNextUpload();
        return;
    }
    emitTransferProgress(transferId, 0, totalBytes, 0, totalFiles);
    auto state = std::make_shared<CtlDirectorySendState>();
    state->ctlPath = ctlPath;
    state->cliPath = cliPath;
    state->transferId = transferId;
    state->root = dir;
    state->totalBytes = totalBytes;
    state->totalFiles = totalFiles;
    state->metadataHeaders = std::move(metadataHeaders);
    state->files = std::make_unique<QDirIterator>(
        ctlPath,
        QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);

    const auto next = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weakNext(next);
    *next = [this, state, weakNext]() {
        if (isTransferCancelled(state->transferId) || m_shutdownRequested.load())
        {
            LOG_INFO("Upload directory cancelled: {}", state->ctlPath);
            processNextUpload();
            return;
        }
        if (state->files->hasNext())
        {
            const QFileInfo fileInfo(state->files->next());
            const QString relativePath = state->root.relativeFilePath(fileInfo.absoluteFilePath());
            const QString fullRemotePath = QDir::cleanPath(state->cliPath + "/" + relativePath);
            const auto nextStep = weakNext.lock();
            if (!nextStep)
                return;
            uploadSingleFileAsync(fileInfo.absoluteFilePath(),
                                  fullRemotePath,
                                  state->transferId,
                                  state->transferredBytes,
                                  state->totalBytes,
                                  state->fileCount + 1,
                                  state->totalFiles,
                                  [this, state, nextStep, fileSize = qMax<qint64>(0, fileInfo.size())](bool success) {
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
                                          processNextUpload();
                                          return;
                                      }
                                      else
                                      {
                                          state->hasErrors = true;
                                      }
                                      if (m_callbackDispatcher)
                                          m_callbackDispatcher->post([nextStep]() { (*nextStep)(); });
                                  });
            return;
        }

        if (state->fileCount == 0)
            LOG_INFO("Directory has no files, sent directory metadata only: {}", state->ctlPath);
        else
            LOG_INFO("Uploaded directory: {} -> {} ({} files, hasErrors={})",
                     state->ctlPath, state->cliPath, state->fileCount, state->hasErrors);
        const QJsonObject dirEndHeader = JsonUtil::createObject()
                                             .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                             .add(Constant::KEY_PATH_CTL, state->ctlPath)
                                             .add(Constant::KEY_PATH_CLI, state->cliPath)
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
                                        {
                                            emit recvUploadFileRes(false,
                                                                   state->cliPath,
                                                                   tr("Failed to send directory completion metadata."));
                                            processNextUpload();
                                            return;
                                        }
                                        LOG_INFO("Sent directory: {} -> {} ({} files)",
                                                 state->ctlPath,
                                                 state->cliPath,
                                                 state->fileCount);
                                        emitTransferProgress(state->transferId,
                                                             state->totalBytes,
                                                             state->totalBytes,
                                                             state->totalFiles,
                                                             state->totalFiles);
                                        processNextUpload();
                                    });
    };
    const auto sendMetadata = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weakSendMetadata(sendMetadata);
    *sendMetadata = [this, state, next, weakSendMetadata]() {
        if (isTransferCancelled(state->transferId) || m_shutdownRequested.load())
        {
            processNextUpload();
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
