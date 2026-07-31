/* Control window toolbar actions. */

#include "ui/control/control_window.h"
#include "ui/transfer/file_transfer_window.h"
#include "ui/control/control_window_view_helpers.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <QToolButton>
#include <QVector>
#include <algorithm>

namespace
{
QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(path);
}

bool pathMatchesRecord(const ControlWindow::TransferRecord &record, const QString &path)
{
    const QString key = normalizedPath(path);
    return (!record.transferId.isEmpty() &&
            (normalizedPath(record.sourcePath) == key || normalizedPath(record.targetPath) == key));
}

QString findFallbackTransferId(const QHash<QString, ControlWindow::TransferRecord> &records, const QString &path)
{
    const QString key = normalizedPath(path);
    QString candidate;
    QDateTime newest;
    for (auto it = records.constBegin(); it != records.constEnd(); ++it)
    {
        const ControlWindow::TransferRecord &record = it.value();
        if (record.finished)
            continue;
        if (!pathMatchesRecord(record, key))
            continue;
        if (!newest.isValid() || record.updatedAt > newest)
        {
            newest = record.updatedAt;
            candidate = it.key();
        }
    }
    if (!candidate.isEmpty())
        return candidate;

    int fileNameMatchCount = 0;
    const QString fileName = QFileInfo(key).fileName();
    if (!fileName.isEmpty())
    {
        for (auto it = records.constBegin(); it != records.constEnd(); ++it)
        {
            const ControlWindow::TransferRecord &record = it.value();
            if (record.finished)
                continue;
            const bool matchesFileName =
                QFileInfo(record.sourcePath).fileName() == fileName ||
                QFileInfo(record.targetPath).fileName() == fileName;
            if (!matchesFileName)
                continue;
            ++fileNameMatchCount;
            candidate = it.key();
        }
        if (fileNameMatchCount == 1)
            return candidate;
    }

    int unfinishedCount = 0;
    for (auto it = records.constBegin(); it != records.constEnd(); ++it)
    {
        const ControlWindow::TransferRecord &record = it.value();
        if (record.finished)
            continue;
        ++unfinishedCount;
        if (record.updatedAt.isValid() && (!newest.isValid() || record.updatedAt > newest))
        {
            newest = record.updatedAt;
            candidate = it.key();
        }
    }
    return unfinishedCount == 1 ? candidate : QString();
}

QString formatTransferProgress(const ControlWindow::TransferRecord &record)
{
    const qint64 totalBytes = qMax<qint64>(0, record.totalBytes);
    const qint64 transferredBytes = qBound<qint64>(0, record.transferredBytes,
                                                    totalBytes > 0 ? totalBytes : record.transferredBytes);
    const int totalFiles = qMax(0, record.totalFiles);
    const int transferredFiles = qBound(0, record.transferredFiles,
                                        totalFiles > 0 ? totalFiles : record.transferredFiles);
    const int percent = totalBytes > 0
                            ? qBound(0, static_cast<int>((static_cast<double>(transferredBytes) * 100.0) /
                                                       static_cast<double>(totalBytes)), 100)
                            : 0;

    if (totalFiles > 1)
    {
        return QCoreApplication::translate("ControlWindow", "%1: %2% (%3/%4 files)")
            .arg(record.operation.isEmpty() ? QCoreApplication::translate("ControlWindow", "Transfer") : record.operation)
            .arg(percent)
            .arg(transferredFiles)
            .arg(totalFiles);
    }

    if (totalBytes > 0)
    {
        return QCoreApplication::translate("ControlWindow", "%1: %2%")
            .arg(record.operation.isEmpty() ? QCoreApplication::translate("ControlWindow", "Transfer") : record.operation)
            .arg(percent);
    }

    return QCoreApplication::translate("ControlWindow", "%1: %2")
        .arg(record.operation.isEmpty() ? QCoreApplication::translate("ControlWindow", "Transfer") : record.operation,
             record.finished ? QCoreApplication::translate("ControlWindow", "Completed")
                             : QCoreApplication::translate("ControlWindow", "Working"));
}

QString transferStatusText(const ControlWindow::TransferRecord &record)
{
    if (record.finished)
        return record.status.isEmpty() ? QCoreApplication::translate("ControlWindow", "Completed") : record.status;
    if (record.totalBytes > 0 || record.totalFiles > 0)
        return QCoreApplication::translate("ControlWindow", "Transferring");
    return QCoreApplication::translate("ControlWindow", "Waiting");
}
}

void ControlWindow::onScreenshotClicked()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    QPixmap screenshot = label.pixmap(Qt::ReturnByValue);
#else
    const QPixmap *labelPixmap = label.pixmap();
    QPixmap screenshot = labelPixmap ? *labelPixmap : QPixmap();
#endif
    if (screenshot.isNull())
    {
        LOG_WARN("No image available for screenshot");
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setPixmap(screenshot);

    LOG_INFO("Screenshot copied to clipboard, size: {}x{}",
             screenshot.width(), screenshot.height());

    m_screenshotBtn->setText(tr("Copied"));
    QTimer::singleShot(1000, this, &ControlWindow::restoreScreenshotButtonText);
}

void ControlWindow::onRemoteEncoderChanged(const QString &encoderName, const QString &encoderType)
{
    m_remoteEncoderName = encoderName;
    m_remoteEncoderType = encoderType;
    appendConnectionProgress(tr("Remote encoder selected: %1 (%2)")
                                 .arg(encoderName.isEmpty() ? QStringLiteral("--") : encoderName,
                                      encoderType.isEmpty() ? QStringLiteral("--") : encoderType));
    refreshStatsLabel();
}

void ControlWindow::onRemoteMediaStateChanged(const QString &codec, const QString &captureMethod)
{
    m_remoteVideoCodec = codec;
    m_remoteCaptureMethod = captureMethod;
    appendConnectionProgress(tr("Remote media parameters: video %1, capture %2")
                                 .arg(codec.isEmpty() ? QStringLiteral("--") : codec,
                                      captureMethod.isEmpty() ? QStringLiteral("--") : captureMethod));
    refreshStatsLabel();
}

void ControlWindow::restoreScreenshotButtonText()
{
    m_screenshotBtn->setText(tr("Screenshot"));
}

void ControlWindow::onTransferRecordClicked()
{
    refreshTransferRecordDialog();
    if (!m_transferRecordDialog)
        return;

    m_transferRecordDialog->show();
    m_transferRecordDialog->raise();
    m_transferRecordDialog->activateWindow();
}

void ControlWindow::onFileTransferClicked()
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    FileTransferWindow *fileWindow = new FileTransferWindow(remote_id, remote_pwd_md5, m_ws);
    fileWindow->setAttribute(Qt::WA_DeleteOnClose);
    fileWindow->setWindowTitle(tr("File Transfer - %1").arg(remote_id));
    fileWindow->show();
    fileWindow->raise();
    fileWindow->activateWindow();

    LOG_INFO("Independent file transfer window opened");
}

void ControlWindow::onTransferStarted(const QString &transferId, const QString &sourcePath, const QString &targetPath, const QString &operation)
{
    if (transferId.isEmpty())
        return;

    TransferRecord &record = m_transferRecords[transferId];
    if (record.transferId.isEmpty())
        record.transferId = transferId;
    if (record.sourcePath.isEmpty())
        record.sourcePath = sourcePath;
    if (record.targetPath.isEmpty())
        record.targetPath = targetPath;
    record.operation = operation;
    record.finished = false;
    record.status = tr("Transferring");
    record.updatedAt = QDateTime::currentDateTime();

    if (!sourcePath.isEmpty())
        m_transferPathToId.insert(normalizedPath(sourcePath), transferId);
    if (!targetPath.isEmpty())
        m_transferPathToId.insert(normalizedPath(targetPath), transferId);

    m_transferStatusText = formatTransferProgress(record);
    if (m_transferStatusTimer)
        m_transferStatusTimer->stop();
    refreshStatsLabel();
    refreshTransferRecordDialog();
}

void ControlWindow::onTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes,
                                      int transferredFiles, int totalFiles)
{
    if (transferId.isEmpty())
        return;

    TransferRecord &record = m_transferRecords[transferId];
    if (record.transferId.isEmpty())
        record.transferId = transferId;
    record.transferredBytes = transferredBytes;
    record.totalBytes = totalBytes;
    record.transferredFiles = transferredFiles;
    record.totalFiles = totalFiles;
    record.status = tr("Transferring");
    record.updatedAt = QDateTime::currentDateTime();

    m_transferStatusText = formatTransferProgress(record);
    if (m_transferStatusTimer)
        m_transferStatusTimer->stop();
    refreshStatsLabel();
    refreshTransferRecordDialog();
}

void ControlWindow::onSessionHealthChanged(int state, const QString &message)
{
    if (state == 0)
    {
        onConnectionStatusChanged(message);
        return;
    }

    onConnectionStatusChanged(message);
    if (state != 2)
        return;

    bool failedTransfer = false;
    for (auto it = m_transferRecords.begin(); it != m_transferRecords.end(); ++it)
    {
        TransferRecord &record = it.value();
        if (record.finished)
            continue;
        record.finished = true;
        record.status = tr("Failed");
        record.updatedAt = QDateTime::currentDateTime();
        failedTransfer = true;
    }
    if (failedTransfer)
    {
        m_transferStatusText = tr("Transfer failed: %1").arg(message);
        refreshStatsLabel();
        refreshTransferRecordDialog();
    }
}

void ControlWindow::onTransferFinished(bool status, const QString &filePath, const QString &errorMessage)
{
    const QString key = normalizedPath(filePath);
    QString transferId = m_transferPathToId.value(key);
    if (transferId.isEmpty())
        transferId = findFallbackTransferId(m_transferRecords, key);
    if (transferId.isEmpty())
        return;

    TransferRecord &record = m_transferRecords[transferId];
    if (record.transferId.isEmpty())
        record.transferId = transferId;
    record.finished = true;
    if (status)
    {
        if (record.totalBytes > 0)
            record.transferredBytes = record.totalBytes;
        if (record.totalFiles > 0)
            record.transferredFiles = record.totalFiles;
        record.status = tr("Completed");
    }
    else
    {
        record.status = tr("Failed");
    }
    record.updatedAt = QDateTime::currentDateTime();

    m_transferStatusText = tr("%1: %2")
        .arg(record.operation.isEmpty() ? tr("Transfer") : record.operation,
             status ? tr("Completed") : tr("Failed"));
    if (!status && !errorMessage.isEmpty())
        m_transferStatusText += tr(" (%1)").arg(errorMessage);
    if (!status && !errorMessage.isEmpty() && RuntimeEnvironment::uiAvailable())
    {
        QMessageBox::warning(this,
                             tr("Upload failed"),
                             tr("Failed to upload remote item:\n%1\n\n%2")
                                 .arg(filePath, errorMessage));
    }
    if (m_transferStatusTimer)
        m_transferStatusTimer->start(3000);
    refreshStatsLabel();
    refreshTransferRecordDialog();
}

void ControlWindow::refreshTransferRecordDialog()
{
    if (!m_transferRecordDialog)
    {
        m_transferRecordDialog = new QDialog(this);
        m_transferRecordDialog->setWindowTitle(tr("Transfer records"));
        m_transferRecordDialog->resize(880, 460);
        auto *layout = new QVBoxLayout(m_transferRecordDialog);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);

        m_transferRecordTable = new QTableWidget(m_transferRecordDialog);
        m_transferRecordTable->setColumnCount(6);
        m_transferRecordTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_transferRecordTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_transferRecordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_transferRecordTable->setAlternatingRowColors(false);
        m_transferRecordTable->setHorizontalHeaderLabels(
            {tr("Operation"), tr("Source path"), tr("Target path"), tr("Progress"), tr("Status"), tr("Updated")});
        m_transferRecordTable->horizontalHeader()->setStretchLastSection(false);
        m_transferRecordTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_transferRecordTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_transferRecordTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        m_transferRecordTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_transferRecordTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        m_transferRecordTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        layout->addWidget(m_transferRecordTable);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, m_transferRecordDialog);
        if (QAbstractButton *closeButton = buttons->button(QDialogButtonBox::Close))
            closeButton->setText(tr("Close"));
        connect(buttons, &QDialogButtonBox::rejected, m_transferRecordDialog, &QDialog::hide);
        layout->addWidget(buttons);
    }

    if (!m_transferRecordTable)
        return;

    QVector<TransferRecord> records = m_transferRecords.values().toVector();
    std::sort(records.begin(), records.end(), [](const TransferRecord &a, const TransferRecord &b) {
        return a.updatedAt > b.updatedAt;
    });

    m_transferRecordTable->setRowCount(records.size());
    for (int row = 0; row < records.size(); ++row)
    {
        const TransferRecord &record = records.at(row);
        const QString progress = formatTransferProgress(record);
        const QString updated = record.updatedAt.isValid()
                                    ? record.updatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                    : QStringLiteral("--");

        m_transferRecordTable->setItem(row, 0, new QTableWidgetItem(record.operation));
        m_transferRecordTable->setItem(row, 1, new QTableWidgetItem(record.sourcePath));
        m_transferRecordTable->setItem(row, 2, new QTableWidgetItem(record.targetPath));
        m_transferRecordTable->setItem(row, 3, new QTableWidgetItem(progress));
        m_transferRecordTable->setItem(row, 4, new QTableWidgetItem(transferStatusText(record)));
        m_transferRecordTable->setItem(row, 5, new QTableWidgetItem(updated));
    }
}
