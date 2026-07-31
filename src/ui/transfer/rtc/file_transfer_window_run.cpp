#include "ui/transfer/file_transfer_window.h"

#include "common/constant.h"
#include "ui_file_transfer_window.h"
#include "util/json/json_util.h"
#include "util/config/config_util.h"
#include "ui/transfer/task/transfer_path_util.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QMenu>
#include <QProcess>
#include <QTableWidget>
#include <QUrl>


namespace
{

QString joinPathWithName(const QString &basePath, const QString &name)
{
    if (basePath.isEmpty())
        return name;
    return QDir::cleanPath(QDir(basePath).absoluteFilePath(name));
}

} // namespace


bool FileTransferWindow::isExecutableFileName(const QString &fileName) const
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    return suffix == QStringLiteral("exe") || suffix == QStringLiteral("bat") ||
           suffix == QStringLiteral("cmd") || suffix == QStringLiteral("com") ||
           suffix == QStringLiteral("msi");
#else
    return suffix == QStringLiteral("sh") || suffix == QStringLiteral("run") ||
           suffix == QStringLiteral("appimage") || suffix == QStringLiteral("desktop");
#endif
}


bool FileTransferWindow::isLocalExecutableFile(const QFileInfo &fileInfo) const
{
    if (!fileInfo.isFile())
        return false;
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    return isExecutableFileName(fileInfo.fileName());
#else
    return fileInfo.isExecutable() || isExecutableFileName(fileInfo.fileName());
#endif
}


bool FileTransferWindow::isRemoteExecutableFile(const QTableWidgetItem *item) const
{
    if (!item || item->data(Qt::UserRole).toBool())
        return false;
    const bool hasRemoteExecutableFlag = item->data(Qt::UserRole + 2).toBool();
    if (hasRemoteExecutableFlag)
        return item->data(Qt::UserRole + 1).toBool() || isExecutableFileName(item->text());
    return true;
}


void FileTransferWindow::requestRunFile(bool remoteSide, const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    if (remoteSide)
    {
        QJsonObject obj = JsonUtil::createObject()
                              .add(Constant::KEY_MSGTYPE, Constant::TYPE_RUN_FILE)
                              .add(Constant::KEY_PATH_CLI, filePath)
                              .build();
        emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
        return;
    }

    QProcess::startDetached(filePath, QStringList(), QFileInfo(filePath).absolutePath());
}


void FileTransferWindow::onUploadButtonClicked()
{
    startUploadFromLocalSelection();
}


void FileTransferWindow::onDownloadButtonClicked()
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    const QString localDir = QFileDialog::getExistingDirectory(this, tr("Select download directory"), QDir::homePath());
    if (localDir.isEmpty())
        return;
    startDownloadFromRemoteSelection(localDir);
}


void FileTransferWindow::onLocalDeleteButtonClicked()
{
    deleteSelectedLocalFiles();
}


void FileTransferWindow::onRemoteDeleteButtonClicked()
{
    deleteSelectedRemoteFiles();
}


bool FileTransferWindow::removeLocalPath(const QString &path, QString *errorMessage) const
{
    QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
    {
        if (errorMessage)
            *errorMessage = tr("Path does not exist.");
        return false;
    }

    bool ok = false;
    if (info.isDir() && !info.isSymLink())
    {
        ok = QDir(path).removeRecursively();
        if (!ok && errorMessage)
            *errorMessage = tr("Failed to remove directory.");
    }
    else
    {
        ok = QFile::remove(path);
        if (!ok && errorMessage)
            *errorMessage = tr("Failed to remove file.");
    }
    return ok;
}


void FileTransferWindow::requestRemoteDelete(const QString &path)
{
    if (!connected || path.isEmpty())
        return;

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DELETE)
                          .add(Constant::KEY_PATH_CLI, path)
                          .build();
    rtc::message_variant msgStr(JsonUtil::toCompactBytes(obj).toStdString());
    emit fileTextChannelSendMsg(msgStr);
}


void FileTransferWindow::requestRemoteRename(const QString &path, const QString &newName)
{
    if (!connected || path.isEmpty() || newName.isEmpty())
        return;

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_RENAME)
                          .add(Constant::KEY_PATH_CLI, path)
                          .add(Constant::KEY_NEW_NAME, newName)
                          .build();
    emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
}


void FileTransferWindow::requestRemoteCreate(const QString &path, bool isDirectory)
{
    if (!connected || path.isEmpty())
        return;

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_CREATE)
                          .add(Constant::KEY_PATH_CLI, path)
                          .add(Constant::KEY_IS_DIR, isDirectory)
                          .build();
    emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
}


void FileTransferWindow::createRemoteItem(bool isDirectory)
{
    if (!connected || !RuntimeEnvironment::uiAvailable())
        return;

    const QString title = isDirectory ? tr("New folder") : tr("New file");
    const QString defaultName = isDirectory ? tr("New Folder") : tr("New File.txt");
    const QString name = QInputDialog::getText(this, title, tr("Name"), QLineEdit::Normal, defaultName).trimmed();
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return;

    requestRemoteCreate(QDir::cleanPath(currentRemotePath + "/" + name), isDirectory);
}


void FileTransferWindow::requestRemoteDownload(const QString &remotePath, const QString &localPath, bool isDirectory)
{
    if (!connected || remotePath.isEmpty() || localPath.isEmpty())
        return;

    const QString fileName = QFileInfo(remotePath).fileName();
    const QString sourcePath = remotePath;
    const QString destinationPath = localPath;
    TransferTask task;
    task.transferId = createTransferId();
    task.sourcePath = sourcePath;
    task.destinationPath = destinationPath;
    task.operation = tr("Download");
    task.totalBytes = 0;
    task.totalFiles = isDirectory ? 0 : 1;
    m_transferTasks.insert(task.transferId, task);
    updateTransferTaskUi(m_transferTasks[task.transferId]);

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                          .add(Constant::KEY_PATH_CTL, localPath)
                          .add(Constant::KEY_PATH_CLI, remotePath)
                          .add(Constant::KEY_TRANSFER_ID, task.transferId)
                          .add("isDirectory", isDirectory)
                          .build();
    emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
    LOG_INFO("Requested remote download: {} -> {}", fileName, localPath);
}


void FileTransferWindow::requestRemoteUpload(const QString &localPath, const QString &remotePath)
{
    if (!connected || localPath.isEmpty() || remotePath.isEmpty())
        return;

    int fileCount = 0;
    const qint64 totalBytes = collectDirectoryStats(localPath, &fileCount);
    TransferTask task;
    task.transferId = createTransferId();
    task.sourcePath = localPath;
    task.destinationPath = remotePath;
    task.operation = tr("Upload");
    task.totalBytes = totalBytes;
    task.totalFiles = fileCount;
    m_transferTasks.insert(task.transferId, task);
    updateTransferTaskUi(m_transferTasks[task.transferId]);
    emit uploadFile2CLI(localPath, remotePath, task.transferId);
}


void FileTransferWindow::requestRemoteUploadBatch(const QList<TransferBatchItem> &items)
{
    if (!connected || items.size() < 2)
        return;

    TransferTask batch;
    batch.transferId = createTransferId();
    batch.sourcePath = items.first().sourcePath;
    batch.destinationPath = QFileInfo(items.first().destinationPath).path();
    batch.operation = tr("Upload");
    batch.batchExpectedItems = items.size();

    QStringList childIds;
    int batchItemIndex = 0;
    for (const TransferBatchItem &item : items)
    {
        TransferTask child;
        child.transferId = createTransferId();
        child.sourcePath = item.sourcePath;
        child.destinationPath = item.destinationPath;
        child.operation = batch.operation;
        child.totalBytes = item.totalBytes;
        child.totalFiles = item.totalFiles;
        child.batchParentId = batch.transferId;
        child.batchItemIndex = batchItemIndex++;
        child.directory = item.isDirectory;
        childIds.append(child.transferId);
        m_transferTasks.insert(child.transferId, child);
        registerTransferPath(item.sourcePath, child.transferId);
        registerTransferPath(item.destinationPath, child.transferId);
    }

    m_transferTasks.insert(batch.transferId, batch);
    TransferTask &storedBatch = m_transferTasks[batch.transferId];
    updateBatchTask(storedBatch);
    updateTransferTaskUi(storedBatch);

    for (const QString &childId : childIds)
    {
        const TransferTask &child = m_transferTasks[childId];
        emit uploadFile2CLI(child.sourcePath, child.destinationPath, child.transferId);
    }
}


void FileTransferWindow::requestRemoteDownloadBatch(const QList<TransferBatchItem> &items)
{
    if (!connected || items.size() < 2)
        return;

    TransferTask batch;
    batch.transferId = createTransferId();
    batch.sourcePath = items.first().sourcePath;
    batch.destinationPath = QFileInfo(items.first().destinationPath).path();
    batch.operation = tr("Download");
    batch.batchExpectedItems = items.size();

    QStringList childIds;
    int batchItemIndex = 0;
    for (const TransferBatchItem &item : items)
    {
        TransferTask child;
        child.transferId = createTransferId();
        child.sourcePath = item.sourcePath;
        child.destinationPath = item.destinationPath;
        child.operation = batch.operation;
        child.totalBytes = item.totalBytes;
        child.totalFiles = item.totalFiles;
        child.batchParentId = batch.transferId;
        child.batchItemIndex = batchItemIndex++;
        child.directory = item.isDirectory;
        childIds.append(child.transferId);
        m_transferTasks.insert(child.transferId, child);
        registerTransferPath(item.sourcePath, child.transferId);
        registerTransferPath(item.destinationPath, child.transferId);
    }

    m_transferTasks.insert(batch.transferId, batch);
    TransferTask &storedBatch = m_transferTasks[batch.transferId];
    updateBatchTask(storedBatch);
    updateTransferTaskUi(storedBatch);

    for (const QString &childId : childIds)
    {
        const TransferTask &child = m_transferTasks[childId];
        const QJsonObject obj = JsonUtil::createObject()
                                    .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                                    .add(Constant::KEY_PATH_CTL, child.destinationPath)
                                    .add(Constant::KEY_PATH_CLI, child.sourcePath)
                                    .add(Constant::KEY_TRANSFER_ID, child.transferId)
                                    .add("isDirectory", child.directory)
                                    .build();
        emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
    }
}


QStringList FileTransferWindow::urlsToLocalPaths(const QList<QUrl> &urls) const
{
    QStringList paths;
    for (const QUrl &url : urls)
    {
        if (!url.isLocalFile())
            continue;
        const QString localPath = QDir::cleanPath(url.toLocalFile());
        if (!localPath.isEmpty())
            paths.append(localPath);
    }
    paths.removeDuplicates();
    return paths;
}


QString FileTransferWindow::localDropTargetPath(const QList<QUrl> &urls) const
{
    const QStringList paths = urlsToLocalPaths(urls);
    if (paths.isEmpty())
        return QString();

    QFileInfo first(paths.first());
    return first.isDir() ? first.absoluteFilePath() : first.absolutePath();
}


void FileTransferWindow::startUploadFromLocalSelection()
{
    if (!connected)
        return;

    const QModelIndexList selectedRows = ui->localTable->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return;

    QList<TransferBatchItem> items;
    for (const QModelIndex &index : selectedRows)
    {
        QTableWidgetItem *nameItem = ui->localTable->item(index.row(), 0);
        if (!nameItem || nameItem->text() == QStringLiteral(".."))
            continue;

        const QString fileName = nameItem->text();
        const QString localFullPath = QDir::cleanPath(currentLocalDir.absoluteFilePath(fileName));
        QFileInfo info(localFullPath);
        if (!info.exists())
            continue;

        const QString remotePath = ui->remotePathCombo->currentText();
        const QString remoteFullPath = joinPathWithName(remotePath, fileName);
        int totalFiles = 0;
        const qint64 totalBytes = collectDirectoryStats(localFullPath, &totalFiles);
        items.append({localFullPath, remoteFullPath, info.isDir(), totalBytes, totalFiles});
    }
    if (items.size() == 1)
        requestRemoteUpload(items.first().sourcePath, items.first().destinationPath);
    else if (items.size() > 1)
        requestRemoteUploadBatch(items);
}


void FileTransferWindow::startDownloadFromRemoteSelection(const QString &localBaseDir)
{
    if (!connected || localBaseDir.isEmpty())
        return;

    const QModelIndexList selectedRows = ui->remoteTable->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return;

    QList<TransferBatchItem> items;
    QSet<QString> reservedDownloadTargets;
    for (const QModelIndex &index : selectedRows)
    {
        QTableWidgetItem *nameItem = ui->remoteTable->item(index.row(), 0);
        if (!nameItem || nameItem->text() == QStringLiteral(".."))
            continue;

        const QString fileName = nameItem->text();
        const bool isDirectory = nameItem->data(Qt::UserRole).toBool();
        const QString remoteFullPath = QDir::cleanPath(currentRemotePath + "/" + fileName);
        const QString localFullPath = reserveUniqueDownloadTarget(localBaseDir,
                                                                  fileName,
                                                                  &reservedDownloadTargets);
        qint64 totalBytes = 0;
        for (const QJsonValue &value : remoteFiles)
        {
            const QJsonObject object = value.toObject();
            if (JsonUtil::getString(object, Constant::KEY_NAME) == fileName)
            {
                totalBytes = JsonUtil::getInt64(object, Constant::KEY_FILE_SIZE, 0);
                break;
            }
        }
        items.append({remoteFullPath, localFullPath, isDirectory, totalBytes, isDirectory ? 0 : 1});
    }
    if (items.size() == 1)
        requestRemoteDownload(items.first().sourcePath, items.first().destinationPath, items.first().isDirectory);
    else if (items.size() > 1)
        requestRemoteDownloadBatch(items);
}


void FileTransferWindow::renameSelectedLocalFile()
{
    const QModelIndexList selectedRows = ui->localTable->selectionModel()->selectedRows();
    if (selectedRows.size() != 1)
        return;

    QTableWidgetItem *nameItem = ui->localTable->item(selectedRows.first().row(), 0);
    if (!nameItem || nameItem->text() == QStringLiteral(".."))
        return;

    const QString oldName = nameItem->text();
    const QString newName = QInputDialog::getText(this, tr("Rename"), tr("New name"), QLineEdit::Normal, oldName).trimmed();
    if (newName.isEmpty() || newName == oldName ||
        newName == QStringLiteral(".") || newName == QStringLiteral("..") ||
        newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\')))
        return;

    const QString oldPath = QDir::cleanPath(currentLocalDir.absoluteFilePath(oldName));
    const QString newPath = QDir::cleanPath(currentLocalDir.absoluteFilePath(newName));
    if (!QFile::rename(oldPath, newPath))
        QMessageBox::warning(this, tr("Rename failed"), tr("Failed to rename %1").arg(oldName));
    else
        populateLocalFiles();
}


void FileTransferWindow::renameSelectedRemoteFile()
{
    const QModelIndexList selectedRows = ui->remoteTable->selectionModel()->selectedRows();
    if (selectedRows.size() != 1 || !connected)
        return;

    QTableWidgetItem *nameItem = ui->remoteTable->item(selectedRows.first().row(), 0);
    if (!nameItem || nameItem->text() == QStringLiteral(".."))
        return;

    const QString oldName = nameItem->text();
    const QString newName = QInputDialog::getText(this, tr("Rename"), tr("New name"), QLineEdit::Normal, oldName).trimmed();
    if (newName.isEmpty() || newName == oldName ||
        newName == QStringLiteral(".") || newName == QStringLiteral("..") ||
        newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\')))
        return;

    const QString oldPath = QDir::cleanPath(currentRemotePath + "/" + oldName);
    requestRemoteRename(oldPath, newName);
}


void FileTransferWindow::onLocalTableContextMenuRequested(const QPoint &pos)
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    const int row = ui->localTable->rowAt(pos.y());
    QTableWidgetItem *item = row >= 0 ? ui->localTable->item(row, 0) : nullptr;
    if (!item || item->text() == QStringLiteral(".."))
        return;
    if (!ui->localTable->selectionModel()->isRowSelected(row, QModelIndex()))
    {
        ui->localTable->clearSelection();
        ui->localTable->selectRow(row);
    }

    const QString filePath = QDir::cleanPath(currentLocalDir.absolutePath() + "/" + item->text());
    QFileInfo info(filePath);

    QMenu menu(this);
    QAction *uploadAction = menu.addAction(tr("Upload"));
    QAction *renameAction = menu.addAction(tr("Rename"));
    QAction *deleteAction = menu.addAction(tr("Delete"));
    QAction *runAction = nullptr;
    if (isLocalExecutableFile(info))
        runAction = menu.addAction(tr("Run"));

    QAction *selectedAction = menu.exec(ui->localTable->viewport()->mapToGlobal(pos));
    if (selectedAction == uploadAction)
        startUploadFromLocalSelection();
    else if (selectedAction == renameAction)
        renameSelectedLocalFile();
    else if (selectedAction == deleteAction)
        deleteSelectedLocalFiles();
    else if (selectedAction == runAction)
        requestRunFile(false, filePath);
}


void FileTransferWindow::onRemoteTableContextMenuRequested(const QPoint &pos)
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    const int row = ui->remoteTable->rowAt(pos.y());
    QTableWidgetItem *item = row >= 0 ? ui->remoteTable->item(row, 0) : nullptr;
    if (!connected)
        return;
    if (!item || item->text() == QStringLiteral(".."))
    {
        QMenu menu(this);
        QAction *newFileAction = menu.addAction(tr("New file"));
        QAction *newFolderAction = menu.addAction(tr("New folder"));
        QAction *selectedAction = menu.exec(ui->remoteTable->viewport()->mapToGlobal(pos));
        if (selectedAction == newFileAction)
            createRemoteItem(false);
        else if (selectedAction == newFolderAction)
            createRemoteItem(true);
        return;
    }
    if (!ui->remoteTable->selectionModel()->isRowSelected(row, QModelIndex()))
    {
        ui->remoteTable->clearSelection();
        ui->remoteTable->selectRow(row);
    }

    const bool isDir = item->data(Qt::UserRole).toBool();
    const QString filePath = QDir::cleanPath(currentRemotePath + "/" + item->text());
    QMenu menu(this);
    QAction *newFileAction = menu.addAction(tr("New file"));
    QAction *newFolderAction = menu.addAction(tr("New folder"));
    menu.addSeparator();
    QAction *downloadAction = menu.addAction(tr("Download"));
    QAction *renameAction = menu.addAction(tr("Rename"));
    QAction *deleteAction = menu.addAction(tr("Delete"));
    QAction *runAction = nullptr;
    if (!isDir && isRemoteExecutableFile(item))
        runAction = menu.addAction(tr("Run"));

    QAction *selectedAction = menu.exec(ui->remoteTable->viewport()->mapToGlobal(pos));
    if (selectedAction == newFileAction)
    {
        createRemoteItem(false);
    }
    else if (selectedAction == newFolderAction)
    {
        createRemoteItem(true);
    }
    else if (selectedAction == downloadAction)
    {
        const QString localDir = QFileDialog::getExistingDirectory(this, tr("Select download directory"), QDir::homePath());
        if (!localDir.isEmpty())
            startDownloadFromRemoteSelection(localDir);
    }
    else if (selectedAction == renameAction)
    {
        renameSelectedRemoteFile();
    }
    else if (selectedAction == deleteAction)
    {
        deleteSelectedRemoteFiles();
    }
    else if (selectedAction == runAction)
    {
        requestRunFile(true, filePath);
    }
}


void FileTransferWindow::startRemoteDragFromSelection()
{
    const QModelIndexList selectedRows = ui->remoteTable->selectionModel()->selectedRows();
    if (selectedRows.isEmpty() || !connected)
        return;

    bool hasDirectory = false;
    for (const QModelIndex &index : selectedRows)
    {
        QTableWidgetItem *item = ui->remoteTable->item(index.row(), 0);
        if (item && item->text() != QStringLiteral("..") && item->data(Qt::UserRole).toBool())
        {
            hasDirectory = true;
            break;
        }
    }
    if (hasDirectory)
    {
        const QString localDir = QFileDialog::getExistingDirectory(this, tr("Select download directory"), QDir::homePath());
        if (!localDir.isEmpty())
            startDownloadFromRemoteSelection(localDir);
        return;
    }

    QJsonArray promiseFiles;
    for (const QModelIndex &index : selectedRows)
    {
        QTableWidgetItem *item = ui->remoteTable->item(index.row(), 0);
        if (!item || item->text() == QStringLiteral(".."))
            continue;
        const QString path = QDir::cleanPath(currentRemotePath + "/" + item->text());
        const bool isDirectory = item->data(Qt::UserRole).toBool();
        Q_UNUSED(isDirectory)
        qint64 fileSize = 0;
        for (const QJsonValue &value : remoteFiles)
        {
            const QJsonObject object = value.toObject();
            if (JsonUtil::getString(object, Constant::KEY_NAME) == item->text())
            {
                fileSize = JsonUtil::getInt64(object, Constant::KEY_FILE_SIZE, 0);
                break;
            }
        }
        promiseFiles.append(JsonUtil::createObject()
                                .add(Constant::KEY_PATH, path)
                                .add(Constant::KEY_NAME, item->text())
                                .add(Constant::KEY_RELATIVE_PATH, item->text())
                                .add(Constant::KEY_IS_DIR, isDirectory)
                                .add(Constant::KEY_FILE_SIZE, static_cast<double>(fileSize))
                                .build());
    }

    if (promiseFiles.isEmpty())
        return;

    QString errorMessage;
    if (m_rtc_ctl.startRemoteFileDrag(this, promiseFiles, createTransferId(), &errorMessage))
        return;

    if (errorMessage.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive))
        return;

    const QString localDir = QFileDialog::getExistingDirectory(this, tr("Select download directory"), QDir::homePath());
    if (!localDir.isEmpty())
        startDownloadFromRemoteSelection(localDir);
    else if (RuntimeEnvironment::uiAvailable() && !errorMessage.isEmpty())
    {
        QMessageBox::warning(this,
                             tr("Download drag failed"),
                             errorMessage);
    }
}
