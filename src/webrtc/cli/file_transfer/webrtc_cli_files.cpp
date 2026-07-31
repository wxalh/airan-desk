#include "webrtc/cli/webrtc_cli.h"
#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"
#include "security/audit_session.h"

#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>


void WebRtcCli::populateLocalFiles()
{
    
    QJsonArray mountedPaths;
    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : volumes)
    {
        if (volume.isValid() && volume.isReady())
        {
            mountedPaths.append(volume.rootPath());
        }
    }

    m_currentDir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    m_currentDir.setSorting(QDir::Name | QDir::DirsFirst);

    QFileInfoList list = m_currentDir.entryInfoList();

    QJsonArray fileArray;
    for (const QFileInfo &entry : list)
    {
        QJsonObject fileObj = JsonUtil::createObject()
                                  .add(Constant::KEY_NAME, entry.fileName())
                                  .add(Constant::KEY_IS_DIR, entry.isDir())
                                  .add(Constant::KEY_FILE_SIZE, static_cast<double>(entry.size()))
                                  .add(Constant::KEY_FILE_SUFFIX, entry.isFile() ? entry.suffix().toLower() : QString())
                                  .add(Constant::KEY_FILE_EXECUTABLE, entry.isFile() && entry.isExecutable())
                                  .add(Constant::KEY_FILE_LAST_MOD_TIME, entry.lastModified().toString(Qt::ISODate))
                                  .build();
        fileArray.append(fileObj);
    }

    QJsonObject responseMsg = JsonUtil::createObject()
                                  .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                  .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_LIST)
                                  .add(Constant::KEY_PATH, m_currentDir.absolutePath())
                                  .add(Constant::KEY_FOLDER_FILES, fileArray)
                                  .add(Constant::KEY_FOLDER_MOUNTED, mountedPaths)
                                  .build();

    sendFileTextChannelMessage(responseMsg);
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

    populateLocalFiles();
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
    m_pendingDownloads.enqueue({cliPath, ctlPath, transferId});
    processDownloadQueue();
}


void WebRtcCli::processDownloadQueue()
{
    if (m_downloadQueueActive)
        return;

    m_downloadQueueActive = true;
    while (!m_pendingDownloads.isEmpty())
    {
        const PendingDownload download = m_pendingDownloads.dequeue();
        if (isTransferCancelled(download.transferId))
            continue;
        sendFile(download.cliPath, download.ctlPath, download.transferId);
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

    QFileInfo info(path);
    bool ok = false;
    QString errorMessage;
    if (!info.exists() && !info.isSymLink())
    {
        errorMessage = tr("Path does not exist.");
    }
    else if (info.isDir() && !info.isSymLink())
    {
        ok = QDir(path).removeRecursively();
        if (!ok)
            errorMessage = tr("Failed to remove directory.");
    }
    else
    {
        ok = QFile::remove(path);
        if (!ok)
            errorMessage = tr("Failed to remove file.");
    }

    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DELETE)
                               .add(Constant::KEY_PATH_CLI, path)
                               .add(Constant::KEY_STATUS, ok)
                               .add(Constant::KEY_ERROR, errorMessage)
                               .build();
    sendFileTextChannelMessage(response);
    if (m_auditSession)
        m_auditSession->recordFileOperation(QStringLiteral("delete"), path, ok, errorMessage);
    LOG_INFO("Remote delete request {}: path={}, error={}",
             ok ? "succeeded" : "failed", path, errorMessage);

    if (ok)
    {
        const QString parentPath = info.absoluteDir().absolutePath();
        if (!parentPath.isEmpty())
            m_currentDir.setPath(parentPath);
        populateLocalFiles();
    }
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

    QFileInfo info(path);
    const QString newPath = info.dir().absoluteFilePath(newName);
    bool ok = false;
    QString errorMessage;
    if (!info.exists())
    {
        errorMessage = tr("Path does not exist.");
    }
    else if (QFileInfo::exists(newPath))
    {
        errorMessage = tr("Target already exists.");
    }
    else
    {
        ok = QDir().rename(path, newPath);
        if (!ok)
            errorMessage = tr("Failed to rename item.");
    }

    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_RENAME)
                               .add(Constant::KEY_PATH_CLI, path)
                               .add(Constant::KEY_PATH, newPath)
                               .add(Constant::KEY_STATUS, ok)
                               .add(Constant::KEY_ERROR, errorMessage)
                               .build();
    sendFileTextChannelMessage(response);
    if (m_auditSession)
        m_auditSession->recordFileOperation(QStringLiteral("rename"), path, ok,
                                            ok ? QFileInfo(newPath).fileName() : errorMessage);
    LOG_INFO("Remote rename request {}: {} -> {}", ok ? "succeeded" : "failed", path, newPath);

    if (ok)
    {
        const QString parentPath = QFileInfo(newPath).absoluteDir().absolutePath();
        if (!parentPath.isEmpty())
            m_currentDir.setPath(parentPath);
        populateLocalFiles();
    }
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

    bool ok = false;
    QString errorMessage;
    QFileInfo info(path);
    if (info.exists() || info.isSymLink())
    {
        errorMessage = tr("Target already exists.");
    }
    else if (isDirectory)
    {
        ok = QDir().mkpath(path);
        if (!ok)
            errorMessage = tr("Failed to create directory.");
    }
    else
    {
        QDir parentDir = info.absoluteDir();
        if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral(".")))
        {
            errorMessage = tr("Failed to create target directory.");
        }
        else
        {
            QFile file(path);
            ok = file.open(QIODevice::WriteOnly);
            if (ok)
                file.close();
            else
                errorMessage = tr("Failed to create file.");
        }
    }

    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_CREATE)
                               .add(Constant::KEY_PATH_CLI, path)
                               .add(Constant::KEY_IS_DIR, isDirectory)
                               .add(Constant::KEY_STATUS, ok)
                               .add(Constant::KEY_ERROR, errorMessage)
                               .build();
    sendFileTextChannelMessage(response);
    LOG_INFO("Remote create request {}: path={}, directory={}, error={}",
             ok ? "succeeded" : "failed", path, isDirectory, errorMessage);

    if (ok)
    {
        const QString parentPath = QFileInfo(path).absoluteDir().absolutePath();
        if (!parentPath.isEmpty())
            m_currentDir.setPath(parentPath);
        populateLocalFiles();
    }
}
