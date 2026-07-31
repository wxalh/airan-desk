#include "ui/transfer/file_transfer_window.h"

#include "ui_file_transfer_window.h"
#include "util/config/config_util.h"

#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>


void FileTransferWindow::recvDownloadFile(bool status, const QString &filePath)
{
    finishTransferTask(QString(), status, filePath);
    if (status)
        populateLocalFiles();
}


void FileTransferWindow::recvUploadFileRes(bool status, const QString &filePath, const QString &errorMessage)
{
    if (status)
    {
        on_remotePathCombo_textActivated(ui->remotePathCombo->currentText());
    }
    else if (RuntimeEnvironment::uiAvailable())
    {
        QMessageBox::warning(this,
                             tr("Upload failed"),
                             tr("Failed to upload remote item:\n%1\n\n%2")
                                 .arg(filePath, errorMessage.isEmpty() ? tr("Unknown") : errorMessage));
    }
    finishTransferTask(QString(), status, filePath);
}


void FileTransferWindow::recvDeleteFileRes(bool status, const QString &filePath, const QString &errorMessage)
{
    if (status || !RuntimeEnvironment::uiAvailable())
        return;

    const QString reason = errorMessage.isEmpty() ? tr("Unknown") : errorMessage;
    QMessageBox::warning(this, tr("Delete failed"),
                         tr("Failed to delete remote item:\n%1\n\n%2").arg(filePath, reason));
}


void FileTransferWindow::recvRenameFileRes(bool status, const QString &filePath, const QString &errorMessage)
{
    if (status)
    {
        on_remotePathCombo_textActivated(ui->remotePathCombo->currentText());
        return;
    }

    if (!RuntimeEnvironment::uiAvailable())
        return;
    const QString reason = errorMessage.isEmpty() ? tr("Unknown") : errorMessage;
    QMessageBox::warning(this, tr("Rename failed"),
                         tr("Failed to rename remote item:\n%1\n\n%2").arg(filePath, reason));
}


void FileTransferWindow::recvCreateFileRes(bool status, const QString &filePath, bool, const QString &errorMessage)
{
    if (status)
    {
        on_remotePathCombo_textActivated(ui->remotePathCombo->currentText());
        return;
    }

    if (!RuntimeEnvironment::uiAvailable())
        return;
    const QString reason = errorMessage.isEmpty() ? tr("Unknown") : errorMessage;
    QMessageBox::warning(this, tr("Create failed"),
                         tr("Failed to create remote item:\n%1\n\n%2").arg(filePath, reason));
}

void FileTransferWindow::onTransferStarted(const QString &transferId, const QString &sourcePath, const QString &targetPath, const QString &operation)
{
    if (transferId.isEmpty() || m_transferTasks.contains(transferId))
        return;

    TransferTask task;
    task.transferId = transferId;
    task.sourcePath = sourcePath;
    task.destinationPath = targetPath;
    task.operation = operation;
    task.totalBytes = 0;
    task.totalFiles = 1;
    m_transferTasks.insert(task.transferId, task);
    updateTransferTaskUi(m_transferTasks[task.transferId]);
}


void FileTransferWindow::onTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes,
                                            int transferredFiles, int totalFiles)
{
    TransferTask *task = findTransferTaskById(transferId);
    if (!task || task->canceled || task->completed)
        return;

    task->transferredBytes = qMax<qint64>(0, transferredBytes);
    if (totalBytes >= 0)
        task->totalBytes = totalBytes;
    task->transferredFiles = qMax(0, transferredFiles);
    if (totalFiles >= 0)
        task->totalFiles = totalFiles;
    if (!task->batchParentId.isEmpty())
    {
        TransferTask *batch = findTransferTaskById(task->batchParentId);
        if (batch && !batch->canceled && !batch->completed)
        {
            if (batch->row >= 0)
                ui->transferLogTable->setItem(batch->row, 3, new QTableWidgetItem(tr("Transferring")));
            updateBatchTask(*batch);
        }
        return;
    }
    if (task->row >= 0)
        ui->transferLogTable->setItem(task->row, 3, new QTableWidgetItem(tr("Transferring")));
    updateProgressCell(*task);
}


void FileTransferWindow::onTransferCancelClicked()
{
    auto *button = qobject_cast<QPushButton *>(sender());
    if (!button)
        return;
    cancelTransferTask(button->property("transferId").toString());
}
