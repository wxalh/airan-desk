#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "security/audit_session.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"
#include "util/text/convert_util.h"

#include <QFileInfo>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include <exception>

namespace
{
struct FileStatResult
{
    QString path;
    bool exists{false};
    bool isFile{false};
    bool isDirectory{false};
    qint64 size{0};
};

class FileStatTask final : public QRunnable
{
public:
    using Operation = std::function<FileStatResult()>;
    using Completion = std::function<void(const FileStatResult &)>;

    FileStatTask(QPointer<QtCallbackDispatcher> dispatcher,
                 std::shared_ptr<CallbackLifetime> callbackLifetime,
                 Operation operation,
                 Completion completion)
        : m_dispatcher(std::move(dispatcher)),
          m_callbackLifetime(std::move(callbackLifetime)),
          m_operation(std::move(operation)),
          m_completion(std::move(completion))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        FileStatResult result;
        try
        {
            result = m_operation ? m_operation() : FileStatResult();
        }
        catch (...)
        {
        }
        auto permit = m_callbackLifetime->tryEnter();
        if (!permit || !m_dispatcher || !m_completion)
            return;
        m_dispatcher->post([completion = std::move(m_completion),
                            callbackLifetime = m_callbackLifetime,
                            result]() mutable {
            auto callbackPermit = callbackLifetime->tryEnter();
            if (callbackPermit && completion)
                completion(result);
        });
    }

private:
    QPointer<QtCallbackDispatcher> m_dispatcher;
    std::shared_ptr<CallbackLifetime> m_callbackLifetime;
    Operation m_operation;
    Completion m_completion;
};
}

void WebRtcCli::sendFile(const QString &cliPath, const QString &ctlPath, const QString &transferId)
{
    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const QPointer<QtCallbackDispatcher> dispatcher(m_callbackDispatcher);
    auto operation = [cliPath]() {
        FileStatResult result;
        result.path = cliPath;
        const QFileInfo info(cliPath);
        result.exists = info.exists();
        result.isFile = info.isFile();
        result.isDirectory = info.isDir();
        result.size = result.isFile ? info.size() : 0;
        return result;
    };
    auto completion = [guard, ctlPath, transferId](const FileStatResult &result) {
        if (!guard)
            return;
        if (!result.exists)
        {
            LOG_ERROR("File or directory does not exist: {}", result.path);
            guard->sendFileErrorResponse(result.path, "File or directory does not exist");
            guard->processNextDownload();
        }
        else if (result.isFile)
        {
            guard->sendSingleFileAsync(result.path, ctlPath, transferId,
                                       0, result.size, 1, 1, {}, result.size);
        }
        else if (result.isDirectory)
        {
            guard->sendDirectory(result.path, ctlPath, transferId);
        }
        else
        {
            LOG_ERROR("Unknown file type: {}", result.path);
            guard->sendFileErrorResponse(result.path, "Unknown file type");
            guard->processNextDownload();
        }
    };
    QThreadPool::globalInstance()->start(
        new FileStatTask(dispatcher, callbackLifetime, std::move(operation), std::move(completion)));
}


void WebRtcCli::sendSingleFileAsync(const QString &cliPath, const QString &ctlPath, const QString &transferId,
                                    qint64 baseBytes, qint64 totalBytes, int currentFileIndex, int totalFiles,
                                    const std::function<void(bool)> &completion, qint64 fileSizeHint)
{
    const auto finish = [this, completion](bool success) {
        if (completion)
            completion(success);
        else
            processNextDownload();
    };
    QFileInfo fileInfo;
    qint64 fileSize = fileSizeHint;
    if (fileSizeHint < 0)
    {
        fileInfo.setFile(cliPath);
        fileSize = fileInfo.isFile() ? fileInfo.size() : -1;
    }
    if (fileSize < 0 || (fileSizeHint < 0 && (!fileInfo.exists() || !fileInfo.isFile())))
    {
        sendFileErrorResponse(cliPath, "File does not exist or is not a regular file");
        if (m_auditSession)
            m_auditSession->recordFileTransfer(cliPath, 0, QStringLiteral("download"), QString(), false);
        finish(false);
        return;
    }
    if (!m_fileChannel || !m_fileChannel->isOpen())
    {
        sendFileErrorResponse(cliPath, "File channel not available");
        finish(false);
        return;
    }

    const QString absCtlPath = QDir::cleanPath(ctlPath);
    const QJsonObject header = JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                                   .add(Constant::KEY_PATH_CLI, cliPath)
                                   .add(Constant::KEY_PATH_CTL, absCtlPath)
                                   .add(Constant::KEY_TRANSFER_ID, transferId)
                                   .add(Constant::KEY_FILE_SIZE, static_cast<double>(fileSize))
                                   .add(Constant::KEY_TRANSFER_TOTAL_BYTES,
                                     static_cast<double>(totalBytes >= 0 ? totalBytes : fileSize))
                                   .add(Constant::KEY_TRANSFER_TOTAL_FILES, totalFiles)
                                   .add("transferBaseBytes", static_cast<double>(baseBytes))
                                   .add("transferFileIndex", currentFileIndex)
                                   .add("isDirectory", false)
                                   .build();
    const qint64 effectiveTotalBytes = totalBytes >= 0 ? totalBytes : fileSize;
    const auto lastProgressMs = std::make_shared<qint64>(0);
    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const auto progressCallback = [guard, callbackLifetime, transferId, baseBytes, effectiveTotalBytes, currentFileIndex,
                                   totalFiles, fileSize, lastProgressMs, absCtlPath, cliPath](
                                      qint64 sentBytes, qint64 packetTotalBytes) {
        auto permit = callbackLifetime->tryEnter();
        if (!permit || !guard)
            return;
        const qint64 headerBytes = qMax<qint64>(0, packetTotalBytes - fileSize);
        const qint64 currentFileBytes = qBound<qint64>(0, sentBytes - headerBytes, fileSize);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (currentFileBytes < fileSize && nowMs - *lastProgressMs < 120)
            return;
        *lastProgressMs = nowMs;
        const bool completedFile = currentFileBytes >= fileSize;
        if (guard && guard->m_callbackDispatcher)
            guard->m_callbackDispatcher->post([guard, callbackLifetime, transferId, baseBytes, effectiveTotalBytes,
                                                currentFileIndex, totalFiles, currentFileBytes,
                                                completedFile,
                                                absCtlPath, cliPath]() {
            auto permit = callbackLifetime->tryEnter();
            if (permit && guard)
                guard->sendTransferProgress(transferId,
                                            qMin(baseBytes + currentFileBytes, effectiveTotalBytes),
                                            effectiveTotalBytes,
                                            completedFile ? currentFileIndex : qMax(0, currentFileIndex - 1),
                                            totalFiles,
                                            absCtlPath,
                                            cliPath);
            });
    };
    const auto cancelCallback = [guard, callbackLifetime, transferId]() {
        auto permit = callbackLifetime->tryEnter();
        if (!permit)
            return true;
        return !guard || guard->m_shutdownRequested.load() || guard->m_shutdownStarted.load() ||
               guard->isTransferCancelled(transferId);
    };
    const auto completionCallback = [guard, callbackLifetime, finish, cliPath, absCtlPath, transferId, fileSize](
                                        bool success, const QString &checksum) {
        auto permit = callbackLifetime->tryEnter();
        if (!permit || !guard)
            return;
        if (guard && guard->m_callbackDispatcher)
            guard->m_callbackDispatcher->post([guard, callbackLifetime, finish, cliPath, absCtlPath, transferId, fileSize, success, checksum]() {
            auto permit = callbackLifetime->tryEnter();
            if (!permit || !guard)
                return;
            if (success)
            {
                LOG_INFO("Sent file stream: {} -> {} ({})", cliPath, absCtlPath, ConvertUtil::formatFileSize(fileSize));
                if (guard->m_auditSession)
                    guard->m_auditSession->recordFileTransfer(cliPath, fileSize, QStringLiteral("download"), checksum, true);
            }
            else if (!guard->isTransferCancelled(transferId))
            {
                guard->sendFileErrorResponse(cliPath, "Failed to send file stream");
                if (guard->m_auditSession)
                    guard->m_auditSession->recordFileTransfer(cliPath, fileSize, QStringLiteral("download"), QString(), false);
            }
            finish(success);
            });
    };

    FilePacketUtil::sendFileStreamAsync(cliPath,
                                        header,
                                        m_fileChannel,
                                        progressCallback,
                                        cancelCallback,
                                        completionCallback,
                                        m_auditSession != nullptr);
}
