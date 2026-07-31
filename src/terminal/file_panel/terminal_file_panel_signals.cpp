#include "terminal/file_panel/terminal_file_panel.h"

#include "common/constant.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"
#include "util/text/convert_util.h"
#include "ui/transfer/task/transfer_batch_progress.h"

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QProgressBar>
#include <QProcess>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QUuid>
#include <QVector>


void TerminalFilePanel::connectUiSignals()
{
    connect(m_remotePathEdit, &QLineEdit::returnPressed, this, &TerminalFilePanel::onPathEditingFinished);
    connect(m_remotePathEdit, &QLineEdit::editingFinished, this, &TerminalFilePanel::onPathEditingFinished);
    connect(m_parentButton, &QToolButton::clicked, this, &TerminalFilePanel::onParentClicked);
    connect(m_refreshButton, &QToolButton::clicked, this, &TerminalFilePanel::onRefreshClicked);
    connect(m_downloadButton, &QToolButton::clicked, this, &TerminalFilePanel::onDownloadClicked);
    connect(m_uploadFileButton, &QToolButton::clicked, this, &TerminalFilePanel::onUploadFileClicked);
    connect(m_uploadDirectoryButton, &QToolButton::clicked, this, &TerminalFilePanel::onUploadDirectoryClicked);
    connect(m_driveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TerminalFilePanel::onDriveChanged);
    connect(m_remoteTable, &QTableWidget::cellDoubleClicked, this, &TerminalFilePanel::onRemoteCellDoubleClicked);
    connect(m_remoteTable, &QTableWidget::customContextMenuRequested, this, &TerminalFilePanel::onRemoteTableContextMenuRequested);
    m_remoteTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_remoteTable->setAcceptDrops(true);
    m_remoteTable->viewport()->setAcceptDrops(true);
    m_remoteTable->setDragEnabled(true);
    m_remoteTable->setDragDropMode(QAbstractItemView::DragDrop);
    m_remoteTable->viewport()->installEventFilter(this);
}


bool TerminalFilePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_remoteTable->viewport())
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        m_remoteDragStartPos = mouseEvent->pos();
    }
    else if (event->type() == QEvent::MouseMove)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if ((mouseEvent->buttons() & Qt::LeftButton) &&
            (mouseEvent->pos() - m_remoteDragStartPos).manhattanLength() >= QApplication::startDragDistance())
        {
            startRemoteDragFromSelection();
            return true;
        }
    }
    else if (event->type() == QEvent::Drop)
    {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        onFilesDropped(dropEvent->mimeData()->urls());
        dropEvent->acceptProposedAction();
        return true;
    }
    else if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)
    {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (dropEvent->mimeData()->hasUrls())
        {
            dropEvent->acceptProposedAction();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}


void TerminalFilePanel::onRemoteTableContextMenuRequested(const QPoint &pos)
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    const int row = m_remoteTable->rowAt(pos.y());
    QTableWidgetItem *item = row >= 0 ? m_remoteTable->item(row, 0) : nullptr;
    if (!m_connected)
        return;
    if (!item || item->text() == QStringLiteral(".."))
    {
        QMenu menu(this);
        QAction *newFileAction = menu.addAction(tr("New file"));
        QAction *newFolderAction = menu.addAction(tr("New folder"));
        QAction *selected = menu.exec(m_remoteTable->viewport()->mapToGlobal(pos));
        if (selected == newFileAction)
            createRemoteItem(false);
        else if (selected == newFolderAction)
            createRemoteItem(true);
        return;
    }
    if (!m_remoteTable->selectionModel()->isRowSelected(row, QModelIndex()))
    {
        m_remoteTable->clearSelection();
        m_remoteTable->selectRow(row);
    }

    const bool isDir = item->data(Qt::UserRole).toBool();
    const QString filePath = joinRemotePath(m_currentRemotePath, item->text());
    QMenu menu(this);
    QAction *newFileAction = menu.addAction(tr("New file"));
    QAction *newFolderAction = menu.addAction(tr("New folder"));
    menu.addSeparator();
    QAction *downloadAction = menu.addAction(tr("Download"));
    QAction *renameAction = menu.addAction(tr("Rename"));
    QAction *deleteAction = menu.addAction(tr("Delete"));
    QAction *runAction = nullptr;
    if (!isDir)
        runAction = menu.addAction(tr("Run"));

    QAction *selected = menu.exec(m_remoteTable->viewport()->mapToGlobal(pos));
    if (selected == newFileAction)
        createRemoteItem(false);
    else if (selected == newFolderAction)
        createRemoteItem(true);
    else if (selected == downloadAction)
        downloadSelectedRemoteFiles();
    else if (selected == renameAction)
        renameSelectedRemoteFile();
    else if (selected == deleteAction)
        deleteSelectedRemoteFiles();
    else if (selected == runAction)
        requestRemoteRunFile(filePath);
}


void TerminalFilePanel::onFilesDropped(const QList<QUrl> &urls)
{
    const QStringList paths = [&urls]() {
        QStringList out;
        for (const QUrl &url : urls)
        {
            if (url.isLocalFile())
                out.append(url.toLocalFile());
        }
        out.removeDuplicates();
        return out;
    }();
    if (paths.isEmpty())
        return;

    uploadFiles(paths);
}


void TerminalFilePanel::startRemoteDragFromSelection()
{
    if (!m_connected)
        return;

    const QModelIndexList selectedRows = m_remoteTable->selectionModel()->selectedRows();
    for (const QModelIndex &index : selectedRows)
    {
        QTableWidgetItem *item = m_remoteTable->item(index.row(), 0);
        if (item && item->text() != QStringLiteral("..") && item->data(Qt::UserRole).toBool())
        {
            downloadSelectedRemoteFiles();
            return;
        }
    }

    QJsonArray promiseFiles;
    for (const QModelIndex &index : selectedRows)
    {
        QTableWidgetItem *item = m_remoteTable->item(index.row(), 0);
        if (!item || item->text() == QStringLiteral(".."))
            continue;

        const QString remotePath = joinRemotePath(m_currentRemotePath, item->text());
        const bool isDirectory = item->data(Qt::UserRole).toBool();
        Q_UNUSED(isDirectory)
        qint64 fileSize = 0;
        for (const QJsonValue &value : m_remoteFiles)
        {
            const QJsonObject object = value.toObject();
            if (JsonUtil::getString(object, Constant::KEY_NAME) == item->text())
            {
                fileSize = JsonUtil::getInt64(object, Constant::KEY_FILE_SIZE, 0);
                break;
            }
        }

        promiseFiles.append(JsonUtil::createObject()
                                .add(Constant::KEY_PATH, remotePath)
                                .add(Constant::KEY_NAME, item->text())
                                .add(Constant::KEY_RELATIVE_PATH, item->text())
                                .add(Constant::KEY_IS_DIR, isDirectory)
                                .add(Constant::KEY_FILE_SIZE, static_cast<double>(fileSize))
                                .build());
    }

    if (promiseFiles.isEmpty())
        return;

    QString errorMessage;
    if (requestRemoteDrag(promiseFiles, createTransferId(), &errorMessage))
        return;
    if (errorMessage.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive))
        return;
    downloadSelectedRemoteFiles();
}


QString TerminalFilePanel::createTransferId() const
{
    QString uuid = QUuid::createUuid().toString();
    if (uuid.startsWith(QLatin1Char('{')) && uuid.endsWith(QLatin1Char('}')))
        uuid = uuid.mid(1, uuid.size() - 2);
    return uuid;
}


void TerminalFilePanel::beginTransfer(const QString &transferId,
                                      const QString &operation,
                                      const TransferBatchItem &item)
{
    if (transferId.isEmpty())
        return;

    TransferTask &task = m_transferTasks[transferId];
    task.operation = operation;
    task.sourcePath = item.sourcePath;
    task.targetPath = item.targetPath;
    task.transferredBytes = 0;
    task.totalBytes = item.totalBytes;
    task.transferredFiles = 0;
    task.totalFiles = item.totalFiles;
    task.completed = false;
    task.status = false;
    task.directory = item.directory;
    registerTransferPath(task.sourcePath, transferId);
    registerTransferPath(task.targetPath, transferId);
    m_currentTransferId = transferId;
    updateTransferStatus();
}


QString TerminalFilePanel::beginTransferBatch(const QString &operation,
                                              const QList<TransferBatchItem> &items,
                                              QStringList *childIds)
{
    if (childIds)
        childIds->clear();
    if (items.size() < 2)
        return QString();

    const QString batchId = createTransferId();
    TransferTask batch;
    batch.operation = operation;
    batch.sourcePath = items.first().sourcePath;
    batch.targetPath = QFileInfo(items.first().targetPath).path();
    batch.batchExpectedItems = items.size();

    int batchItemIndex = 0;
    for (const TransferBatchItem &item : items)
    {
        const QString childId = createTransferId();
        TransferTask &child = m_transferTasks[childId];
        child.operation = operation;
        child.sourcePath = item.sourcePath;
        child.targetPath = item.targetPath;
        child.totalBytes = item.totalBytes;
        child.totalFiles = item.totalFiles;
        child.batchParentId = batchId;
        child.batchItemIndex = batchItemIndex++;
        child.directory = item.directory;
        registerTransferPath(child.sourcePath, childId);
        registerTransferPath(child.targetPath, childId);
        if (childIds)
            childIds->append(childId);
    }

    m_transferTasks.insert(batchId, batch);
    TransferTask &storedBatch = m_transferTasks[batchId];
    updateBatchTransfer(batchId, storedBatch);
    m_currentTransferId = batchId;
    updateTransferStatus();
    return batchId;
}


QString TerminalFilePanel::findTransferIdByPath(const QString &path)
{
    const QString cleanPath = QDir::cleanPath(path);
    auto mappedIt = m_transferPathIds.find(cleanPath);
    if (mappedIt != m_transferPathIds.end())
    {
        QStringList &transferIds = mappedIt.value();
        while (!transferIds.isEmpty())
        {
            auto taskIt = m_transferTasks.find(transferIds.first());
            if (taskIt != m_transferTasks.end() && !taskIt->completed)
                return transferIds.first();
            transferIds.removeFirst();
        }
        m_transferPathIds.erase(mappedIt);
    }

    for (auto it = m_transferTasks.begin(); it != m_transferTasks.end(); ++it)
    {
        if (!it->completed && (it->targetPath == path || it->sourcePath == path))
            return it.key();
    }
    return QString();
}


void TerminalFilePanel::registerTransferPath(const QString &path, const QString &transferId)
{
    if (path.isEmpty() || transferId.isEmpty())
        return;
    QStringList &transferIds = m_transferPathIds[QDir::cleanPath(path)];
    if (!transferIds.contains(transferId))
        transferIds.append(transferId);
}


void TerminalFilePanel::unregisterTransferPaths(const QString &transferId, const TransferTask &task)
{
    const QStringList paths{QDir::cleanPath(task.sourcePath), QDir::cleanPath(task.targetPath)};
    for (const QString &path : paths)
    {
        auto mappedIt = m_transferPathIds.find(path);
        if (mappedIt == m_transferPathIds.end())
            continue;
        mappedIt.value().removeAll(transferId);
        if (mappedIt.value().isEmpty())
            m_transferPathIds.erase(mappedIt);
    }
}


void TerminalFilePanel::updateBatchTransfer(const QString &batchId, TransferTask &task)
{
    if (task.batchExpectedItems <= 0 || !task.batchParentId.isEmpty())
        return;

    QVector<TransferBatchProgressItem> children;
    for (auto it = m_transferTasks.cbegin(); it != m_transferTasks.cend(); ++it)
    {
        const TransferTask &child = it.value();
        if (child.batchParentId != batchId)
            continue;
        children.append({child.batchItemIndex,
                         child.completed,
                         child.transferredBytes,
                         child.totalBytes,
                         child.transferredFiles,
                         child.totalFiles});
    }

    const TransferBatchProgress progress = aggregateTransferBatchProgress(children);
    task.transferredBytes = progress.transferredBytes;
    task.totalBytes = progress.totalBytes;
    task.transferredFiles = progress.transferredFiles;
    task.totalFiles = progress.totalFiles;
    task.currentFile = progress.currentFile;
}


void TerminalFilePanel::onTransferStarted(const QString &transferId, const QString &sourcePath, const QString &targetPath, const QString &operation)
{
    if (transferId.isEmpty() || m_transferTasks.contains(transferId))
        return;
    beginTransfer(transferId, operation, {sourcePath, targetPath, false, 0, 1});
}


void TerminalFilePanel::finishTransfer(const QString &transferId, bool status, const QString &path)
{
    auto it = m_transferTasks.find(transferId);
    if (it == m_transferTasks.end())
        return;

    if (it->completed)
        return;
    it->completed = true;
    it->status = status;
    Q_UNUSED(path)
    unregisterTransferPaths(transferId, it.value());
    if (status && it->totalBytes > 0)
        it->transferredBytes = it->totalBytes;
    if (status && it->totalFiles > 0)
        it->transferredFiles = it->totalFiles;

    if (!it->batchParentId.isEmpty())
    {
        const QString batchId = it->batchParentId;
        auto batchIt = m_transferTasks.find(batchId);
        if (batchIt == m_transferTasks.end() || batchIt->completed)
            return;
        ++batchIt->batchCompletedItems;
        batchIt->batchFailed = batchIt->batchFailed || !status;
        m_currentTransferId = batchId;
        updateBatchTransfer(batchId, batchIt.value());
        if (batchIt->batchCompletedItems >= batchIt->batchExpectedItems)
        {
            batchIt->completed = true;
            batchIt->status = !batchIt->batchFailed;
        }
    }
    else
    {
        m_currentTransferId = transferId;
    }
    updateTransferStatus();
}


void TerminalFilePanel::updateTransferStatus()
{
    if (!m_transferStatusLabel || !m_transferProgressBar)
        return;

    if (m_transferTasks.isEmpty())
    {
        m_transferStatusLabel->setText(tr("Transfer idle"));
        m_transferProgressBar->setValue(0);
        return;
    }

    auto currentIt = m_transferTasks.constFind(m_currentTransferId);
    const TransferTask *task = currentIt != m_transferTasks.cend() ? &currentIt.value() : nullptr;
    if (!task)
    {
        for (auto it = m_transferTasks.cbegin(); it != m_transferTasks.cend(); ++it)
        {
            if (!it.value().completed)
            {
                task = &it.value();
                m_currentTransferId = it.key();
                break;
            }
        }
    }
    if (!task)
    {
        auto it = m_transferTasks.cbegin();
        task = &it.value();
        m_currentTransferId = it.key();
    }

    if (task->totalBytes > 0)
    {
        const qint64 transferredBytes = qBound<qint64>(0, task->transferredBytes, task->totalBytes);
        const int percent = qBound(0, static_cast<int>((static_cast<double>(transferredBytes) * 100.0) /
                                                       static_cast<double>(task->totalBytes)), 100);
        m_transferProgressBar->setValue(percent);
        const int currentFile = task->currentFile > 0
                                    ? qMin(task->totalFiles, task->currentFile)
                                    : (task->completed
                                           ? task->transferredFiles
                                           : qMin(task->totalFiles, task->transferredFiles + 1));
        QString statusText;
        if (task->totalFiles > 1)
        {
            statusText = tr("%1: (%2/%3) %4% (%5/%6)")
                             .arg(task->operation)
                             .arg(currentFile)
                             .arg(task->totalFiles)
                             .arg(percent)
                             .arg(ConvertUtil::formatFileSize(task->transferredBytes))
                             .arg(ConvertUtil::formatFileSize(task->totalBytes));
        }
        else
        {
            statusText = tr("%1: %2% (%3/%4)")
                             .arg(task->operation)
                             .arg(percent)
                             .arg(ConvertUtil::formatFileSize(task->transferredBytes))
                             .arg(ConvertUtil::formatFileSize(task->totalBytes));
        }
        if (task->completed)
            statusText += task->status ? tr(" - completed") : tr(" - failed");
        m_transferStatusLabel->setText(statusText);
    }
    else
    {
        m_transferProgressBar->setValue(task->completed && task->status ? 100 : 0);
        if (task->totalFiles > 1)
        {
            const int currentFile = task->currentFile > 0
                                        ? qMin(task->totalFiles, task->currentFile)
                                        : qMin(task->totalFiles, task->transferredFiles + 1);
            if (task->completed)
            {
                m_transferStatusLabel->setText(task->status
                                                   ? tr("%1 completed (%2/%3)")
                                                         .arg(task->operation)
                                                         .arg(currentFile)
                                                         .arg(task->totalFiles)
                                                   : tr("%1 failed (%2/%3)")
                                                         .arg(task->operation)
                                                         .arg(currentFile)
                                                         .arg(task->totalFiles));
            }
            else
            {
                m_transferStatusLabel->setText(tr("%1: (%2/%3)")
                                                   .arg(task->operation)
                                                   .arg(currentFile)
                                                   .arg(task->totalFiles));
            }
        }
        else
        {
            m_transferStatusLabel->setText(task->completed
                                               ? (task->status
                                                      ? tr("%1 completed").arg(task->operation)
                                                      : tr("%1 failed").arg(task->operation))
                                               : tr("%1 in progress").arg(task->operation));
        }
    }
}

void TerminalFilePanel::abortTransfers(const QString &reason)
{
    bool aborted = false;
    for (auto it = m_transferTasks.begin(); it != m_transferTasks.end(); ++it)
    {
        if (it->completed)
            continue;
        it->completed = true;
        it->status = false;
        unregisterTransferPaths(it.key(), it.value());
        aborted = true;
    }
    updateTransferStatus();
    if (aborted && m_transferStatusLabel)
        m_transferStatusLabel->setText(tr("Transfer failed: %1").arg(reason));
}
