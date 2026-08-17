#include "webrtc/cli/webrtc_cli.h"
#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"
#include "security/audit_session.h"

#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QCoreApplication>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QStorageInfo>

#include <algorithm>

namespace
{
constexpr int kMaxPendingDownloadRequests = 1024;
constexpr int kMaxPendingFileMutationTasks = 32;
constexpr int kMaxTransferPathChars = 32 * 1024;
constexpr int kMaxTransferIdChars = 256;
constexpr int kMaxFileListEntries = 5000;

struct FileListScanResult
{
    QJsonArray mountedPaths;
    QJsonArray files;
    QString error;
};

struct FileMutationResult
{
    QString path;
    QString newPath;
    QString parentPath;
    QString error;
    bool status{false};
};

FileMutationResult deleteFileMutation(const QString &path)
{
    FileMutationResult result;
    result.path = path;
    const QFileInfo info(path);
    result.parentPath = info.absoluteDir().absolutePath();
    if (!info.exists() && !info.isSymLink())
    {
        result.error = QCoreApplication::translate("WebRtcCli", "Path does not exist.");
    }
    else if (info.isDir() && !info.isSymLink())
    {
        result.status = QDir(path).removeRecursively();
        if (!result.status)
            result.error = QCoreApplication::translate("WebRtcCli", "Failed to remove directory.");
    }
    else
    {
        result.status = QFile::remove(path);
        if (!result.status)
            result.error = QCoreApplication::translate("WebRtcCli", "Failed to remove file.");
    }
    return result;
}

FileMutationResult renameFileMutation(const QString &path, const QString &newPath)
{
    FileMutationResult result;
    result.path = path;
    result.newPath = newPath;
    result.parentPath = QFileInfo(newPath).absoluteDir().absolutePath();
    const QFileInfo info(path);
    if (!info.exists())
    {
        result.error = QCoreApplication::translate("WebRtcCli", "Path does not exist.");
    }
    else if (QFileInfo::exists(newPath))
    {
        result.error = QCoreApplication::translate("WebRtcCli", "Target already exists.");
    }
    else
    {
        result.status = QDir().rename(path, newPath);
        if (!result.status)
            result.error = QCoreApplication::translate("WebRtcCli", "Failed to rename item.");
    }
    return result;
}

FileMutationResult createFileMutation(const QString &path, bool isDirectory)
{
    FileMutationResult result;
    result.path = path;
    result.parentPath = QFileInfo(path).absoluteDir().absolutePath();
    const QFileInfo info(path);
    if (info.exists() || info.isSymLink())
    {
        result.error = QCoreApplication::translate("WebRtcCli", "Target already exists.");
    }
    else if (isDirectory)
    {
        result.status = QDir().mkpath(path);
        if (!result.status)
            result.error = QCoreApplication::translate("WebRtcCli", "Failed to create directory.");
    }
    else
    {
        QDir parentDir = info.absoluteDir();
        if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral(".")))
        {
            result.error = QCoreApplication::translate("WebRtcCli", "Failed to create target directory.");
        }
        else
        {
            QFile file(path);
            result.status = file.open(QIODevice::WriteOnly);
            if (result.status)
                file.close();
            else
                result.error = QCoreApplication::translate("WebRtcCli", "Failed to create file.");
        }
    }
    return result;
}

FileListScanResult scanFileList(const QString &path)
{
    FileListScanResult result;
    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes())
    {
        if (volume.isValid() && volume.isReady())
            result.mountedPaths.append(volume.rootPath());
    }

    const QFileInfo directoryInfo(path);
    if (!directoryInfo.exists() || !directoryInfo.isDir() || !directoryInfo.isReadable())
    {
        result.error = QCoreApplication::translate(
            "WebRtcCli", "Remote directory is unavailable or not readable.");
        return result;
    }

    QFileInfoList entries;
    QDirIterator iterator(path,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::NoIteratorFlags);
    while (iterator.hasNext())
    {
        if (entries.size() >= kMaxFileListEntries)
        {
            result.error = QCoreApplication::translate(
                "WebRtcCli", "Directory contains too many entries to display safely.");
            return result;
        }
        entries.append(QFileInfo(iterator.next()));
    }

    std::sort(entries.begin(), entries.end(), [](const QFileInfo &left, const QFileInfo &right) {
        if (left.isDir() != right.isDir())
            return left.isDir();
        return QString::compare(left.fileName(), right.fileName(), Qt::CaseInsensitive) < 0;
    });
    for (const QFileInfo &entry : entries)
    {
        result.files.append(JsonUtil::createObject()
                                .add(Constant::KEY_NAME, entry.fileName())
                                .add(Constant::KEY_IS_DIR, entry.isDir())
                                .add(Constant::KEY_FILE_SIZE, static_cast<double>(entry.size()))
                                .add(Constant::KEY_FILE_SUFFIX, entry.isFile() ? entry.suffix().toLower() : QString())
                                .add(Constant::KEY_FILE_EXECUTABLE, entry.isFile() && entry.isExecutable())
                                .add(Constant::KEY_FILE_LAST_MOD_TIME, entry.lastModified().toString(Qt::ISODate))
                                .build());
    }
    return result;
}

class FileListTask final : public QRunnable
{
public:
    FileListTask(QString path, QPointer<QtCallbackDispatcher> dispatcher,
                 std::shared_ptr<CallbackLifetime> callbackLifetime,
                 std::function<void(FileListScanResult)> completion)
        : m_path(std::move(path)),
          m_dispatcher(std::move(dispatcher)),
          m_callbackLifetime(std::move(callbackLifetime)),
          m_completion(std::move(completion))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        const FileListScanResult result = scanFileList(m_path);
        auto permit = m_callbackLifetime->tryEnter();
        if (!permit || !m_dispatcher || !m_completion)
            return;
        m_dispatcher->post([completion = std::move(m_completion), result]() mutable {
            completion(result);
        });
    }

private:
    QString m_path;
    QPointer<QtCallbackDispatcher> m_dispatcher;
    std::shared_ptr<CallbackLifetime> m_callbackLifetime;
    std::function<void(FileListScanResult)> m_completion;
};

class FileMutationTask final : public QRunnable
{
public:
    using Operation = std::function<FileMutationResult()>;
    using Completion = std::function<void(const FileMutationResult &)>;

    FileMutationTask(QPointer<QtCallbackDispatcher> dispatcher,
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
        FileMutationResult result;
        try
        {
            result = m_operation ? m_operation() : FileMutationResult();
        }
        catch (const std::exception &error)
        {
            result.error = QString::fromLocal8Bit(error.what());
        }
        catch (...)
        {
            result.error = QStringLiteral("File operation failed.");
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


void WebRtcCli::populateLocalFiles(const QString &requestId)
{
    const QString path = m_currentDir.absolutePath();
    const quint64 requestGeneration = ++m_fileListGeneration;
    if (m_fileListScanRunning)
    {
        m_fileListScanPending = true;
        // A background refresh must not erase a newer explicit request from
        // the controller. Keep the queued request ID unless this call carries
        // a real request ID itself.
        if (!requestId.isEmpty() || m_pendingFileListRequestId.isEmpty())
            m_pendingFileListRequestId = requestId;
        return;
    }
    m_fileListScanRunning = true;
    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const QPointer<QtCallbackDispatcher> dispatcher(m_callbackDispatcher);
    const auto completion = [guard, callbackLifetime, path, requestId, requestGeneration](FileListScanResult result) mutable {
        auto permit = callbackLifetime->tryEnter();
        if (!permit || !guard)
            return;
        guard->m_fileListScanRunning = false;
        const bool stale = requestGeneration != guard->m_fileListGeneration;
        if (!stale)
        {
            QJsonObject responseMsg = JsonUtil::createObject()
                                          .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_LIST)
                                          .add(Constant::KEY_PATH, path)
                                          .add(Constant::KEY_STATUS, result.error.isEmpty())
                                          .add(Constant::KEY_ERROR, result.error)
                                          .add(Constant::KEY_FOLDER_MOUNTED, result.mountedPaths)
                                          .build();
            if (result.error.isEmpty())
                responseMsg.insert(Constant::KEY_FOLDER_FILES, result.files);
            if (!requestId.isEmpty())
                responseMsg.insert(Constant::KEY_REQUEST_ID, requestId);
            guard->sendFileTextChannelMessage(responseMsg);
        }
        if (guard->m_fileListScanPending)
        {
            const QString nextRequestId = guard->m_pendingFileListRequestId;
            guard->m_fileListScanPending = false;
            guard->m_pendingFileListRequestId.clear();
            guard->populateLocalFiles(nextRequestId);
        }
    };
    QThreadPool::globalInstance()->start(
        new FileListTask(path, dispatcher, callbackLifetime, completion));
}


void WebRtcCli::parseFileMsg(const QJsonObject &object)
{
    const QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
    if (msgType.isEmpty())
    {
        LOG_ERROR("parseFileMsg: Missing msgType");
        return;
    }

    if (QThread::currentThread() != thread())
    {
        if (msgType == Constant::TYPE_FILE_TRANSFER_CANCEL)
            markTransferCancelled(JsonUtil::getString(object, Constant::KEY_TRANSFER_ID));
        QMetaObject::invokeMethod(this, "parseFileMsg",
                                  Qt::QueuedConnection,
                                  Q_ARG(QJsonObject, object));
        return;
    }

    if (msgType == Constant::TYPE_FILE_LIST)
    {
        handleFileListRequest(object);
    }
    else if (msgType == Constant::TYPE_FILE_DOWNLOAD)
    {
        handleFileDownloadRequest(object);
    }
    else if (msgType == Constant::TYPE_FILE_UPLOAD)
    {
        
        LOG_INFO("File upload request received, waiting for binary data on file channel");
    }
    else if (msgType == Constant::TYPE_FILE_DELETE)
    {
        handleFileDeleteRequest(object);
    }
    else if (msgType == Constant::TYPE_FILE_RENAME)
    {
        handleFileRenameRequest(object);
    }
    else if (msgType == Constant::TYPE_FILE_CREATE)
    {
        handleFileCreateRequest(object);
    }
    else if (msgType == Constant::TYPE_RUN_FILE)
    {
        handleRunFile(object);
    }
    else if (msgType == Constant::TYPE_FILE_TRANSFER_CANCEL)
    {
        handleFileTransferCancel(object);
    }
    else if (msgType == Constant::TYPE_TERMINAL_START ||
             msgType == Constant::TYPE_TERMINAL_INPUT ||
             msgType == Constant::TYPE_TERMINAL_RESIZE ||
             msgType == Constant::TYPE_TERMINAL_STOP ||
             msgType == Constant::TYPE_TERMINAL_FLOW_CONTROL)
    {
        handleTerminalMessage(object);
    }
    else
    {
        LOG_WARNING("parseFileMsg: Unknown message type: {}", msgType);
    }
}


void WebRtcCli::handleFileListRequest(const QJsonObject &object)
{
    QString path = JsonUtil::getString(object, Constant::KEY_PATH);
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    LOG_INFO("Processing file list request for path: {}", path);
    if (path.isEmpty())
    {
        LOG_ERROR("parseFileMsg: Missing path for file list request");
        return;
    }
    if (path == Constant::FOLDER_HOME)
        m_currentDir = QDir::home();
    else
        m_currentDir.setPath(path);

    populateLocalFiles(requestId);
}


void WebRtcCli::handleFileDownloadRequest(const QJsonObject &object)
{
    QString cliPath = JsonUtil::getString(object, Constant::KEY_PATH_CLI);
    QString ctlPath = JsonUtil::getString(object, Constant::KEY_PATH_CTL);
    QString transferId = JsonUtil::getString(object, Constant::KEY_TRANSFER_ID);
    if (cliPath.isEmpty() || ctlPath.isEmpty())
    {
        LOG_ERROR("parseFileMsg: Missing file paths for download request");
        return;
    }
    if (cliPath.size() > kMaxTransferPathChars || ctlPath.size() > kMaxTransferPathChars ||
        transferId.size() > kMaxTransferIdChars ||
        m_pendingDownloads.size() >= kMaxPendingDownloadRequests)
    {
        LOG_ERROR("Rejected download request because the pending queue or fields exceed limits: queued={}, cliChars={}, ctlChars={}, transferIdChars={}",
                  m_pendingDownloads.size(), cliPath.size(), ctlPath.size(), transferId.size());
        m_disconnectReason = QStringLiteral("download_queue_overflow");
        emit destroyCli();
        return;
    }
    m_pendingDownloads.enqueue({cliPath, ctlPath, transferId});
    processDownloadQueue();
}


void WebRtcCli::processDownloadQueue()
{
    if (m_downloadQueueActive)
        return;

    m_downloadQueueActive = true;
    processNextDownload();
}


void WebRtcCli::processNextDownload()
{
    if (m_shutdownRequested.load())
    {
        m_downloadQueueActive = false;
        return;
    }

    while (!m_pendingDownloads.isEmpty() && !m_shutdownRequested.load())
    {
        const PendingDownload download = m_pendingDownloads.dequeue();
        if (isTransferCancelled(download.transferId))
            continue;

        sendFile(download.cliPath, download.ctlPath, download.transferId);
        return;
    }
    m_downloadQueueActive = false;
}


void WebRtcCli::handleFileTransferCancel(const QJsonObject &object)
{
    const QString transferId = JsonUtil::getString(object, Constant::KEY_TRANSFER_ID);
    markTransferCancelled(transferId);
    if (m_filePacketUtil)
        m_filePacketUtil->cancelTransfer(transferId);
}


void WebRtcCli::handleFileDeleteRequest(const QJsonObject &object)
{
    const QString path = JsonUtil::getString(object, Constant::KEY_PATH_CLI,
                                            JsonUtil::getString(object, Constant::KEY_PATH));
    if (path.isEmpty())
    {
        LOG_WARN("parseFileMsg: Missing path for delete request");
        return;
    }
    if (m_activeFileMutationTasks >= kMaxPendingFileMutationTasks)
    {
        LOG_WARN("Rejecting file delete because the mutation task limit was reached: active={}",
                 m_activeFileMutationTasks);
        m_disconnectReason = QStringLiteral("file_mutation_queue_overflow");
        emit destroyCli();
        return;
    }
    ++m_activeFileMutationTasks;

    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const QPointer<QtCallbackDispatcher> dispatcher(m_callbackDispatcher);
    auto operation = [path]() { return deleteFileMutation(path); };
    auto completion = [guard](const FileMutationResult &result) {
        if (!guard)
            return;
        guard->m_activeFileMutationTasks = qMax(0, guard->m_activeFileMutationTasks - 1);
        QJsonObject response = JsonUtil::createObject()
                                   .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DELETE)
                                   .add(Constant::KEY_PATH_CLI, result.path)
                                   .add(Constant::KEY_STATUS, result.status)
                                   .add(Constant::KEY_ERROR, result.error)
                                   .build();
        guard->sendFileTextChannelMessage(response);
        if (guard->m_auditSession)
            guard->m_auditSession->recordFileOperation(QStringLiteral("delete"), result.path,
                                                        result.status, result.error);
        LOG_INFO("Remote delete request {}: path={}, error={}",
                 result.status ? "succeeded" : "failed", result.path, result.error);
        if (result.status && !result.parentPath.isEmpty())
        {
            guard->m_currentDir.setPath(result.parentPath);
            guard->populateLocalFiles();
        }
    };
    QThreadPool::globalInstance()->start(
        new FileMutationTask(dispatcher, callbackLifetime, std::move(operation), std::move(completion)));
}


void WebRtcCli::handleFileRenameRequest(const QJsonObject &object)
{
    const QString path = JsonUtil::getString(object, Constant::KEY_PATH_CLI,
                                            JsonUtil::getString(object, Constant::KEY_PATH));
    const QString newName = JsonUtil::getString(object, Constant::KEY_NEW_NAME);
    if (path.isEmpty() || newName.isEmpty() ||
        newName == QStringLiteral(".") || newName == QStringLiteral("..") ||
        newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\')))
    {
        LOG_WARN("parseFileMsg: Invalid path or new name for rename request");
        return;
    }

    if (m_activeFileMutationTasks >= kMaxPendingFileMutationTasks)
    {
        LOG_WARN("Rejecting file rename because the mutation task limit was reached: active={}",
                 m_activeFileMutationTasks);
        m_disconnectReason = QStringLiteral("file_mutation_queue_overflow");
        emit destroyCli();
        return;
    }
    ++m_activeFileMutationTasks;

    const QString newPath = QFileInfo(path).dir().absoluteFilePath(newName);
    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const QPointer<QtCallbackDispatcher> dispatcher(m_callbackDispatcher);
    auto operation = [path, newPath]() { return renameFileMutation(path, newPath); };
    auto completion = [guard](const FileMutationResult &result) {
        if (!guard)
            return;
        guard->m_activeFileMutationTasks = qMax(0, guard->m_activeFileMutationTasks - 1);
        QJsonObject response = JsonUtil::createObject()
                                   .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_RENAME)
                                   .add(Constant::KEY_PATH_CLI, result.path)
                                   .add(Constant::KEY_PATH, result.newPath)
                                   .add(Constant::KEY_STATUS, result.status)
                                   .add(Constant::KEY_ERROR, result.error)
                                   .build();
        guard->sendFileTextChannelMessage(response);
        if (guard->m_auditSession)
            guard->m_auditSession->recordFileOperation(
                QStringLiteral("rename"), result.path, result.status,
                result.status ? QFileInfo(result.newPath).fileName() : result.error);
        LOG_INFO("Remote rename request {}: {} -> {}",
                 result.status ? "succeeded" : "failed", result.path, result.newPath);
        if (result.status && !result.parentPath.isEmpty())
        {
            guard->m_currentDir.setPath(result.parentPath);
            guard->populateLocalFiles();
        }
    };
    QThreadPool::globalInstance()->start(
        new FileMutationTask(dispatcher, callbackLifetime, std::move(operation), std::move(completion)));
}

void WebRtcCli::handleFileCreateRequest(const QJsonObject &object)
{
    const QString path = JsonUtil::getString(object, Constant::KEY_PATH_CLI,
                                            JsonUtil::getString(object, Constant::KEY_PATH));
    const bool isDirectory = JsonUtil::getBool(object, Constant::KEY_IS_DIR, false);
    if (path.isEmpty())
    {
        LOG_WARN("parseFileMsg: Missing path for create request");
        return;
    }

    if (m_activeFileMutationTasks >= kMaxPendingFileMutationTasks)
    {
        LOG_WARN("Rejecting file create because the mutation task limit was reached: active={}",
                 m_activeFileMutationTasks);
        m_disconnectReason = QStringLiteral("file_mutation_queue_overflow");
        emit destroyCli();
        return;
    }
    ++m_activeFileMutationTasks;

    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const QPointer<QtCallbackDispatcher> dispatcher(m_callbackDispatcher);
    auto operation = [path, isDirectory]() { return createFileMutation(path, isDirectory); };
    auto completion = [guard, isDirectory](const FileMutationResult &result) {
        if (!guard)
            return;
        guard->m_activeFileMutationTasks = qMax(0, guard->m_activeFileMutationTasks - 1);
        QJsonObject response = JsonUtil::createObject()
                                   .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_CREATE)
                                   .add(Constant::KEY_PATH_CLI, result.path)
                                   .add(Constant::KEY_IS_DIR, isDirectory)
                                   .add(Constant::KEY_STATUS, result.status)
                                   .add(Constant::KEY_ERROR, result.error)
                                   .build();
        guard->sendFileTextChannelMessage(response);
        LOG_INFO("Remote create request {}: path={}, directory={}, error={}",
                 result.status ? "succeeded" : "failed", result.path, isDirectory, result.error);
        if (result.status && !result.parentPath.isEmpty())
        {
            guard->m_currentDir.setPath(result.parentPath);
            guard->populateLocalFiles();
        }
    };
    QThreadPool::globalInstance()->start(
        new FileMutationTask(dispatcher, callbackLifetime, std::move(operation), std::move(completion)));
}
