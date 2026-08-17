#include "terminal/file_panel/terminal_file_panel.h"

#include "common/logger_manager.h"
#include "common/constant.h"
#include "util/json/json_util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QToolButton>

namespace
{
QString normalizedRemotePath(const QString &path)
{
    const QString raw = path.trimmed();
    const bool windowsStyle = raw.size() >= 2 && raw.at(1) == QLatin1Char(':') &&
                              raw.at(0).isLetter();
    const bool uncStyle = raw.startsWith(QStringLiteral("//")) ||
                          raw.startsWith(QStringLiteral("\\\\"));
    QString normalizedInput = QDir::fromNativeSeparators(raw);
    if (windowsStyle || uncStyle)
        normalizedInput.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QString normalized = QDir::cleanPath(normalizedInput);
    // The controller may run on a different OS than the remote endpoint.
    // Treat Windows drive and UNC paths case-insensitively regardless of the local build.
    if (windowsStyle || uncStyle)
        normalized = normalized.toLower();
    return normalized;
}
}


QString TerminalFilePanel::currentRemotePath() const
{
    return m_currentRemotePath;
}


void TerminalFilePanel::setConnected(bool connected)
{
    m_connected = connected;
    m_remotePathEdit->setEnabled(connected);
    m_parentButton->setEnabled(connected);
    m_refreshButton->setEnabled(connected);
    m_uploadFileButton->setEnabled(connected);
    m_uploadDirectoryButton->setEnabled(connected);
    m_downloadButton->setEnabled(connected);
    m_driveCombo->setEnabled(connected);
    m_followPathCheck->setEnabled(connected);
}


void TerminalFilePanel::setPendingFileListRequestId(const QString &requestId)
{
    m_pendingFileListRequestId = requestId;
}


void TerminalFilePanel::setRemotePath(const QString &path)
{
    if (path.isEmpty())
        return;

    const QString normalizedPath = normalizedRemotePath(path);
    const bool samePath = !m_currentRemotePath.isEmpty() &&
                          normalizedPath == normalizedRemotePath(m_currentRemotePath);
    if (!samePath)
    {
        m_currentRemotePath = path;
        updatePathEdit();
    }

    const bool pathLoaded = !m_loadedRemotePath.isEmpty() &&
                            normalizedPath == normalizedRemotePath(m_loadedRemotePath);
    if (m_connected && (!samePath || (m_pendingFileListRequestId.isEmpty() && !pathLoaded)))
        emit requestFileList(path);
}


void TerminalFilePanel::followTerminalPath(const QString &path)
{
    if (m_followPathCheck && !m_followPathCheck->isChecked())
        return;
    setRemotePath(path);
}


void TerminalFilePanel::recvGetFileList(const QJsonObject &object)
{
    const QString responseRequestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    const QString responsePath = JsonUtil::getString(object, Constant::KEY_PATH);
    const bool responsePathMatches = responsePath.isEmpty() || m_currentRemotePath.isEmpty() ||
                                     normalizedRemotePath(responsePath) == normalizedRemotePath(m_currentRemotePath);
    if (!responsePathMatches)
    {
        LOG_DEBUG("Ignoring stale file-list response: responsePath={}, currentPath={}",
                  responsePath,
                  m_currentRemotePath);
        return;
    }

    if (!m_pendingFileListRequestId.isEmpty())
    {
        const bool requestIdMatches = !responseRequestId.isEmpty() &&
                                      responseRequestId == m_pendingFileListRequestId;
        const bool legacyResponseMatches = responseRequestId.isEmpty() &&
                                           !responsePath.isEmpty() &&
                                           responsePathMatches;
        if (!requestIdMatches && !legacyResponseMatches)
        {
            LOG_DEBUG("Ignoring stale file-list generation: responseRequestId={}, currentRequestId={}",
                      responseRequestId,
                      m_pendingFileListRequestId);
            return;
        }
        m_pendingFileListRequestId.clear();
    }
    if (object.contains(Constant::KEY_STATUS) &&
        !JsonUtil::getBool(object, Constant::KEY_STATUS, true))
    {
        setConnected(true);
        QMessageBox::warning(this,
                             tr("File list failed"),
                             tr("Failed to list remote directory:\n%1")
                                 .arg(JsonUtil::getString(object, Constant::KEY_ERROR, tr("Unknown"))));
        return;
    }
    if (object.contains(Constant::KEY_PATH))
    {
        const QString path = responsePath;
        if (!path.isEmpty())
            m_currentRemotePath = path;
    }
    m_loadedRemotePath = m_currentRemotePath;
    if (object.contains(Constant::KEY_FOLDER_FILES))
    {
        m_remoteFiles = object.value(Constant::KEY_FOLDER_FILES).toArray();
        populateRemoteFiles();
    }
    if (object.contains(Constant::KEY_FOLDER_MOUNTED))
        updateMountedPaths(object.value(Constant::KEY_FOLDER_MOUNTED).toArray());
    setConnected(true);
    updatePathEdit();
    updateDriveCombo();
}


void TerminalFilePanel::recvDownloadFile(bool status, const QString &filePath)
{
    const QString transferId = findTransferIdByPath(filePath);
    if (!transferId.isEmpty())
        finishTransfer(transferId, status, filePath);
}


void TerminalFilePanel::recvUploadFile(bool status, const QString &filePath, const QString &errorMessage)
{
    const QString transferId = findTransferIdByPath(filePath);
    if (!transferId.isEmpty())
        finishTransfer(transferId, status, filePath);
    if (status && !m_currentRemotePath.isEmpty())
        emit requestFileList(m_currentRemotePath);
    else if (!status)
        QMessageBox::warning(this,
                             tr("Upload failed"),
                             tr("Failed to upload remote item:\n%1\n\n%2")
                                 .arg(filePath, errorMessage.isEmpty() ? tr("Unknown") : errorMessage));
}


void TerminalFilePanel::recvRenameFile(bool status, const QString &filePath, const QString &errorMessage)
{
    if (status)
    {
        if (!m_currentRemotePath.isEmpty())
            emit requestFileList(m_currentRemotePath);
        return;
    }
    QMessageBox::warning(this, tr("Rename failed"),
                         tr("Failed to rename remote item:\n%1\n\n%2").arg(filePath, errorMessage));
}


void TerminalFilePanel::recvDeleteFile(bool status, const QString &filePath, const QString &errorMessage)
{
    if (status)
    {
        if (!m_currentRemotePath.isEmpty())
            emit requestFileList(m_currentRemotePath);
        return;
    }
    QMessageBox::warning(this, tr("Delete failed"),
                         tr("Failed to delete remote item:\n%1\n\n%2").arg(filePath, errorMessage));
}


void TerminalFilePanel::recvCreateFile(bool status, const QString &filePath, bool, const QString &errorMessage)
{
    if (status)
    {
        if (!m_currentRemotePath.isEmpty())
            emit requestFileList(m_currentRemotePath);
        return;
    }
    QMessageBox::warning(this, tr("Create failed"),
                         tr("Failed to create remote item:\n%1\n\n%2").arg(filePath, errorMessage));
}


void TerminalFilePanel::onTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes,
                                           int transferredFiles, int totalFiles)
{
    auto it = m_transferTasks.find(transferId);
    if (it == m_transferTasks.end())
        return;

    it->transferredBytes = qMax<qint64>(0, transferredBytes);
    if (totalBytes >= 0)
        it->totalBytes = totalBytes;
    it->transferredFiles = qMax(0, transferredFiles);
    if (totalFiles >= 0)
        it->totalFiles = totalFiles;
    if (!it->batchParentId.isEmpty())
    {
        auto batchIt = m_transferTasks.find(it->batchParentId);
        if (batchIt != m_transferTasks.end() && !batchIt->completed)
        {
            m_currentTransferId = it->batchParentId;
            updateBatchTransfer(it->batchParentId, batchIt.value());
        }
    }
    else
    {
        m_currentTransferId = transferId;
    }
    updateTransferStatus();
}
