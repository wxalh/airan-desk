#include "ui/transfer/file_transfer_window.h"

#include "ui_file_transfer_window.h"

#include <QPushButton>
#include <QTableWidget>


void FileTransferWindow::finishTransferTask(const QString &transferId, bool status, const QString &filePath)
{
    TransferTask *task = findTransferTaskById(transferId);
    if (!task)
        task = findTransferTaskByPath(filePath);
    if (!task || task->canceled || task->completed)
        return;

    if (!task->batchParentId.isEmpty())
    {
        task->completed = true;
        unregisterTransferPaths(*task);
        if (status && task->totalBytes > 0)
            task->transferredBytes = task->totalBytes;
        if (status && task->totalFiles > 0)
            task->transferredFiles = task->totalFiles;

        const QString parentId = task->batchParentId;
        TransferTask *batch = findTransferTaskById(parentId);
        if (!batch || batch->canceled || batch->completed)
            return;
        ++batch->batchCompletedItems;
        batch->batchFailed = batch->batchFailed || !status;
        updateBatchTask(*batch);
        if (batch->batchCompletedItems < batch->batchExpectedItems)
            return;

        task = batch;
        status = !batch->batchFailed;
    }

    if (task->row < 0)
        return;
    task->completed = true;

    if (status && task->totalBytes > 0)
        task->transferredBytes = task->totalBytes;
    if (status && task->totalFiles > 0)
        task->transferredFiles = task->totalFiles;
    updateProgressCell(*task);

    ui->transferLogTable->setItem(task->row, 3, new QTableWidgetItem(status ? tr("Success") : tr("Failed")));
    if (task->cancelButton)
    {
        task->cancelButton->setEnabled(false);
        task->cancelButton->setText(status ? tr("OK") : QStringLiteral("X"));
        task->cancelButton->setToolTip(status ? tr("Success") : tr("Failed"));
        if (status)
            task->cancelButton->setStyleSheet(task->cancelButton->styleSheet() + QStringLiteral("QPushButton#transferActionButton:disabled { color: #77d68a; }"));
        task->cancelButton->setCursor(Qt::ArrowCursor);
    }
}


void FileTransferWindow::cancelTransferTask(const QString &transferId)
{
    TransferTask *task = findTransferTaskById(transferId);
    if (!task)
        return;

    task->canceled = true;
    task->completed = true;
    if (task->batchExpectedItems > 0)
    {
        for (auto it = m_transferTasks.begin(); it != m_transferTasks.end(); ++it)
        {
            TransferTask &child = it.value();
            if (child.batchParentId != task->transferId || child.completed)
                continue;
            child.canceled = true;
            child.completed = true;
            unregisterTransferPaths(child);
            emit cancelFileTransfer(child.transferId);
        }
    }
    ui->transferLogTable->setItem(task->row, 3, new QTableWidgetItem(tr("Canceled")));
    if (task->cancelButton)
    {
        task->cancelButton->setEnabled(false);
        task->cancelButton->setToolTip(tr("Canceled"));
        task->cancelButton->setText(QStringLiteral("X"));
        task->cancelButton->setCursor(Qt::ArrowCursor);
    }
    if (task->batchExpectedItems <= 0)
        emit cancelFileTransfer(transferId);
}

void FileTransferWindow::failActiveTransfers(const QString &reason)
{
    for (auto it = m_transferTasks.begin(); it != m_transferTasks.end(); ++it)
    {
        TransferTask &task = it.value();
        if (task.completed || task.canceled)
            continue;
        task.completed = true;
        unregisterTransferPaths(task);
        if (task.row >= 0)
            ui->transferLogTable->setItem(task.row, 3, new QTableWidgetItem(tr("Failed: %1").arg(reason)));
        if (task.cancelButton)
        {
            task.cancelButton->setEnabled(false);
            task.cancelButton->setText(QStringLiteral("X"));
            task.cancelButton->setToolTip(tr("Failed: %1").arg(reason));
        }
    }
}
