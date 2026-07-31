#include "ui/transfer/file_transfer_window.h"

#include "ui_file_transfer_window.h"
#include "util/text/convert_util.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>


int FileTransferWindow::ensureTransferTaskRow(const TransferTask &task)
{
    if (task.row >= 0)
        return task.row;

    const int row = ui->transferLogTable->rowCount();
    ui->transferLogTable->insertRow(row);
    return row;
}


void FileTransferWindow::updateProgressCell(TransferTask &task)
{
    if (!task.progressLabel)
        return;

    const qint64 totalBytes = qMax<qint64>(0, task.totalBytes);
    const qint64 transferredBytes = qBound<qint64>(0, task.transferredBytes, totalBytes > 0 ? totalBytes : task.transferredBytes);
    const int totalFiles = qMax(0, task.totalFiles);
    const int transferredFiles = qBound(0, task.transferredFiles, totalFiles > 0 ? totalFiles : task.transferredFiles);

    QString progressText;
    if (totalFiles > 1)
    {
        const int currentFile = task.currentFile > 0
                                    ? qMin(totalFiles, task.currentFile)
                                    : (task.completed || task.canceled
                                           ? transferredFiles
                                           : qMin(totalFiles, transferredFiles + 1));
        progressText = tr("(%1/%2) %3 / %4")
                           .arg(currentFile)
                           .arg(totalFiles)
                           .arg(ConvertUtil::formatFileSize(transferredBytes))
                           .arg(ConvertUtil::formatFileSize(totalBytes));
    }
    else
    {
        progressText = tr("%1 / %2")
                           .arg(ConvertUtil::formatFileSize(transferredBytes))
                           .arg(totalBytes > 0 ? ConvertUtil::formatFileSize(totalBytes) : tr("Unknown"));
    }
    task.progressLabel->setText(progressText);
}


void FileTransferWindow::updateTransferTaskUi(TransferTask &task)
{
    if (task.row < 0)
        task.row = ensureTransferTaskRow(task);

    ui->transferLogTable->setItem(task.row, 0, new QTableWidgetItem(task.sourcePath));
    ui->transferLogTable->setItem(task.row, 1, new QTableWidgetItem(task.destinationPath));
    ui->transferLogTable->setItem(task.row, 3, new QTableWidgetItem(tr("Waiting")));

    auto *progressLabel = new QLabel(ui->transferLogTable);
    progressLabel->setObjectName(QStringLiteral("transferProgressLabel"));
    progressLabel->setAlignment(Qt::AlignCenter);
    progressLabel->setStyleSheet(QStringLiteral("background: transparent; color: rgb(131, 193, 224); padding: 0 6px;"));
    ui->transferLogTable->setCellWidget(task.row, 2, progressLabel);
    task.progressLabel = progressLabel;

    auto *cancelButton = new QPushButton(tr("Cancel"), ui->transferLogTable);
    cancelButton->setObjectName(QStringLiteral("transferActionButton"));
    cancelButton->setText(QStringLiteral("X"));
    cancelButton->setToolTip(tr("Cancel"));
    cancelButton->setCursor(Qt::PointingHandCursor);
    cancelButton->setFlat(true);
    cancelButton->setMinimumSize(32, 24);
    cancelButton->setMaximumHeight(24);
    cancelButton->setStyleSheet(QStringLiteral(
        "QPushButton#transferActionButton {"
        "    background: transparent;"
        "    border: none;"
        "    color: #ff7a90;"
        "    font-size: 15px;"
        "    font-weight: 700;"
        "    padding: 0;"
        "}"
        "QPushButton#transferActionButton:hover {"
        "    color: #ffffff;"
        "    background: transparent;"
        "}"
        "QPushButton#transferActionButton:disabled {"
        "    color: #7a7d82;"
        "    background: transparent;"
        "}"));
    cancelButton->setProperty("transferId", task.transferId);
    connect(cancelButton, &QPushButton::clicked, this, &FileTransferWindow::onTransferCancelClicked);

    auto *actionHost = new QWidget(ui->transferLogTable);
    actionHost->setStyleSheet(QStringLiteral("background: #181818;"));
    auto *actionLayout = new QHBoxLayout(actionHost);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(0);
    actionLayout->addStretch(1);
    actionLayout->addWidget(cancelButton);
    actionLayout->addStretch(1);
    ui->transferLogTable->setCellWidget(task.row, 4, actionHost);
    task.cancelButton = cancelButton;

    updateProgressCell(task);
}
