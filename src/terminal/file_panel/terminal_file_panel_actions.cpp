#include "terminal/file_panel/terminal_file_panel.h"

#include "common/constant.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"
#include "ui/transfer/task/transfer_path_util.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QTableWidget>

QStringList TerminalFilePanel::selectedRemotePaths() const
{
    QStringList paths;
    const QModelIndexList selectedRows = m_remoteTable->selectionModel()->selectedRows();
    for (const QModelIndex &index : selectedRows)
    {
        QTableWidgetItem *item = m_remoteTable->item(index.row(), 0);
        if (!item || item->text() == QStringLiteral(".."))
            continue;
        paths.append(joinRemotePath(m_currentRemotePath, item->text()));
    }
    paths.removeDuplicates();
    return paths;
}


void TerminalFilePanel::requestRemoteDelete(const QString &path)
{
    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DELETE)
                          .add(Constant::KEY_PATH_CLI, path)
                          .build();
    emit requestRemoteOperation(msg);
}


void TerminalFilePanel::requestRemoteRename(const QString &path, const QString &newName)
{
    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_RENAME)
                          .add(Constant::KEY_PATH_CLI, path)
                          .add(Constant::KEY_NEW_NAME, newName)
                          .build();
    emit requestRemoteOperation(msg);
}


void TerminalFilePanel::requestRemoteCreate(const QString &path, bool isDirectory)
{
    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_CREATE)
                          .add(Constant::KEY_PATH_CLI, path)
                          .add(Constant::KEY_IS_DIR, isDirectory)
                          .build();
    emit requestRemoteOperation(msg);
}


void TerminalFilePanel::requestRemoteRunFile(const QString &path)
{
    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_RUN_FILE)
                          .add(Constant::KEY_PATH_CLI, path)
                          .build();
    emit requestRemoteOperation(msg);
}


void TerminalFilePanel::deleteSelectedRemoteFiles()
{
    if (!m_connected)
        return;
    const QStringList paths = selectedRemotePaths();
    if (paths.isEmpty())
        return;
    if (RuntimeEnvironment::uiAvailable() && QMessageBox::question(this, tr("Delete"),
                                                    tr("Delete %1 selected remote item(s)?").arg(paths.size())) != QMessageBox::Yes)
        return;
    for (const QString &path : paths)
        requestRemoteDelete(path);
}


void TerminalFilePanel::renameSelectedRemoteFile()
{
    const QStringList paths = selectedRemotePaths();
    if (paths.size() != 1)
        return;
    const QString oldPath = paths.first();
    const QString oldName = QFileInfo(oldPath).fileName();
    const QString newName = QInputDialog::getText(this, tr("Rename"), tr("New name"), QLineEdit::Normal, oldName).trimmed();
    if (newName.isEmpty() || newName == oldName ||
        newName == QStringLiteral(".") || newName == QStringLiteral("..") ||
        newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\')))
        return;
    requestRemoteRename(oldPath, newName);
}


void TerminalFilePanel::createRemoteItem(bool isDirectory)
{
    if (!m_connected)
        return;
    const QString title = isDirectory ? tr("New folder") : tr("New file");
    const QString defaultName = isDirectory ? tr("New Folder") : tr("New File.txt");
    const QString name = QInputDialog::getText(this, title, tr("Name"), QLineEdit::Normal, defaultName).trimmed();
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return;
    requestRemoteCreate(joinRemotePath(m_currentRemotePath, name), isDirectory);
}


void TerminalFilePanel::runSelectedRemoteFile()
{
    const QStringList paths = selectedRemotePaths();
    if (paths.size() != 1)
        return;
    requestRemoteRunFile(paths.first());
}


void TerminalFilePanel::downloadSelectedRemoteFiles()
{
    const QStringList paths = selectedRemotePaths();
    if (paths.isEmpty())
        return;

    const QString localDir = QFileDialog::getExistingDirectory(this, tr("Select download directory"), QDir::homePath());
    if (localDir.isEmpty())
        return;

    QList<TransferBatchItem> items;
    QSet<QString> reservedDownloadTargets;
    for (const QString &remotePath : paths)
    {
        const QString fileName = QFileInfo(remotePath).fileName();
        const QString localPath = reserveUniqueDownloadTarget(localDir,
                                                              fileName,
                                                              &reservedDownloadTargets);
        QTableWidgetItem *item = nullptr;
        for (int row = 0; row < m_remoteTable->rowCount(); ++row)
        {
            QTableWidgetItem *candidate = m_remoteTable->item(row, 0);
            if (candidate && joinRemotePath(m_currentRemotePath, candidate->text()) == remotePath)
            {
                item = candidate;
                break;
            }
        }
        const bool directory = item && item->data(Qt::UserRole).toBool();
        qint64 totalBytes = 0;
        for (const QJsonValue &value : m_remoteFiles)
        {
            const QJsonObject object = value.toObject();
            if (JsonUtil::getString(object, Constant::KEY_NAME) == fileName)
            {
                totalBytes = JsonUtil::getInt64(object, Constant::KEY_FILE_SIZE, 0);
                break;
            }
        }
        items.append({remotePath, localPath, directory, totalBytes, directory ? 0 : 1});
    }

    if (items.size() == 1)
    {
        const QString transferId = createTransferId();
        beginTransfer(transferId, tr("Download"), items.first());
        emit requestDownload(items.first().sourcePath,
                             items.first().targetPath,
                             items.first().directory,
                             transferId);
        return;
    }

    QStringList childIds;
    beginTransferBatch(tr("Download"), items, &childIds);
    for (const QString &childId : childIds)
    {
        const TransferTask &child = m_transferTasks[childId];
        emit requestDownload(child.sourcePath, child.targetPath, child.directory, childId);
    }
}


void TerminalFilePanel::uploadFiles(const QStringList &paths)
{
    if (!m_connected || paths.isEmpty())
        return;
    const QString remoteBasePath = m_currentRemotePath.isEmpty() && m_remotePathEdit
                                       ? m_remotePathEdit->text().trimmed()
                                       : m_currentRemotePath;
    if (remoteBasePath.isEmpty())
        return;

    QList<TransferBatchItem> items;
    QSet<QString> reservedUploadTargets;
    QStringList duplicateUploadTargets;
    for (const QString &path : paths)
    {
        QFileInfo info(path);
        if (!info.exists())
            continue;
        const QString remotePath = joinRemotePath(remoteBasePath, info.fileName());
        if (!reserveTransferTarget(remotePath, &reservedUploadTargets, true))
        {
            duplicateUploadTargets.append(remotePath);
            continue;
        }
        int totalFiles = 0;
        const qint64 totalBytes = collectDirectoryStats(path, &totalFiles);
        items.append({path, remotePath, info.isDir(), totalBytes, totalFiles});
    }

    if (!duplicateUploadTargets.isEmpty() && RuntimeEnvironment::uiAvailable())
    {
        QMessageBox::warning(this,
                             tr("Upload"),
                             tr("Skipped duplicate upload target(s):\n%1")
                                 .arg(duplicateUploadTargets.join(QLatin1Char('\n'))));
    }

    if (items.size() == 1)
    {
        const QString transferId = createTransferId();
        beginTransfer(transferId, tr("Upload"), items.first());
        emit requestUpload(items.first().sourcePath,
                           items.first().targetPath,
                           items.first().directory,
                           transferId);
        return;
    }

    QStringList childIds;
    beginTransferBatch(tr("Upload"), items, &childIds);
    for (const QString &childId : childIds)
    {
        const TransferTask &child = m_transferTasks[childId];
        emit requestUpload(child.sourcePath, child.targetPath, child.directory, childId);
    }
}


void TerminalFilePanel::onDownloadClicked()
{
    downloadSelectedRemoteFiles();
}


void TerminalFilePanel::onUploadFileClicked()
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Select files to upload"), QDir::homePath());
    if (!paths.isEmpty())
        uploadFiles(paths);
}


qint64 TerminalFilePanel::collectDirectoryStats(const QString &path, int *fileCount) const
{
    if (fileCount)
        *fileCount = 0;

    const QFileInfo info(path);
    if (!info.exists())
        return 0;
    if (info.isFile())
    {
        if (fileCount)
            *fileCount = 1;
        return qMax<qint64>(0, info.size());
    }
    return 0;
}


void TerminalFilePanel::onUploadDirectoryClicked()
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    const QString path = QFileDialog::getExistingDirectory(this, tr("Select directory to upload"), QDir::homePath());
    if (!path.isEmpty())
        uploadFiles({path});
}
