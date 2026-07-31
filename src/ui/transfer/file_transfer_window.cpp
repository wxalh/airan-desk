#include "ui/transfer/file_transfer_window.h"
#include "ui/transfer/task/transfer_path_util.h"

#include "common/constant.h"
#include "ui_file_transfer_window.h"
#include "util/config/config_util.h"

#include <QApplication>
#include <QDrag>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QTimer>
#include <QMetaObject>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QTableWidget>
#include <QUuid>


FileTransferWindow::FileTransferWindow(QString remoteId, QString remotePwdMd5, WsCli *_ws_cli,
                                       QWidget *parent)
    : QWidget(parent), ui(new Ui::FileTransferWindow), connected(false), remote_id(remoteId),
      m_instanceId(QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'))),
      remote_pwd_md5(remotePwdMd5), m_rtc_ctl(remoteId, remotePwdMd5, true),
      m_ws(_ws_cli), currentLocalDir(QDir::home())
{
    m_rtc_ctl.setSessionLabel(QStringLiteral("file-window-%1").arg(m_instanceId));
    initUI();
    initCLI();
    emit initRtcCtl();
}


FileTransferWindow::~FileTransferWindow()
{
    m_closing.store(true);
    Q_ASSERT(!m_rtc_ctl_thread.isRunning());
    delete ui;
}

void FileTransferWindow::beginAsyncShutdown()
{
    if (m_closing.exchange(true))
        return;
    connected = false;
    hide();
    if (m_ws)
    {
        disconnect(m_ws, nullptr, &m_rtc_ctl, nullptr);
    }
    disconnect(this, nullptr, &m_rtc_ctl, nullptr);
    disconnect(&m_rtc_ctl, nullptr, this, nullptr);
    if (m_rtc_ctl_thread.isRunning())
    {
        QMetaObject::invokeMethod(&m_rtc_ctl,
                                  "shutdownAndMoveToOwnerThread",
                                  Qt::QueuedConnection,
                                  Q_ARG(QObject *, this));
    }
    finalizeCloseWhenStopped();
}

void FileTransferWindow::finalizeCloseWhenStopped()
{
    if (!m_closing.load() || m_rtc_ctl_thread.isRunning())
        return;
    m_shutdownComplete = true;
    QTimer::singleShot(0, this, [this]() { close(); });
}

bool FileTransferWindow::isClosing() const
{
    return m_closing.load();
}


void FileTransferWindow::closeEvent(QCloseEvent *event)
{
    if (m_shutdownComplete)
    {
        event->accept();
        return;
    }
    event->ignore();
    beginAsyncShutdown();
}

void FileTransferWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        QWidget *focusedWidget = QApplication::focusWidget();
        if (QTableWidget *table = qobject_cast<QTableWidget *>(focusedWidget))
        {
            if (table == ui->localTable)
            {
                QTableWidgetItem *currentItem = table->currentItem();
                if (currentItem)
                    on_localTable_cellDoubleClicked(table->currentRow(), 0);
            }
        }
    }
}


bool FileTransferWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (isClosing())
        return QWidget::eventFilter(watched, event);

    if (watched == ui->localTable->viewport())
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            m_localDragStartPos = mouseEvent->pos();
        }
        else if (event->type() == QEvent::MouseMove)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if ((mouseEvent->buttons() & Qt::LeftButton) &&
                (mouseEvent->pos() - m_localDragStartPos).manhattanLength() >= QApplication::startDragDistance())
            {
                QDrag *drag = new QDrag(this);
                auto *mimeData = new QMimeData();
                QList<QUrl> urls;
                const QModelIndexList selectedRows = ui->localTable->selectionModel()->selectedRows();
                for (const QModelIndex &index : selectedRows)
                {
                    QTableWidgetItem *item = ui->localTable->item(index.row(), 0);
                    if (!item || item->text() == QStringLiteral(".."))
                        continue;
                    const QString path = QDir::cleanPath(currentLocalDir.absoluteFilePath(item->text()));
                    urls.append(QUrl::fromLocalFile(path));
                }
                if (urls.isEmpty())
                    return QWidget::eventFilter(watched, event);
                mimeData->setUrls(urls);
                drag->setMimeData(mimeData);
                drag->exec(Qt::CopyAction | Qt::MoveAction);
                return true;
            }
        }
        else if (event->type() == QEvent::Drop)
        {
            auto *dropEvent = static_cast<QDropEvent *>(event);
            onLocalFilesDropped(dropEvent->mimeData()->urls());
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
    }
    else if (watched == ui->remoteTable->viewport())
    {
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
            onRemoteFilesDropped(dropEvent->mimeData()->urls());
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
    }
    return QWidget::eventFilter(watched, event);
}


void FileTransferWindow::onLocalFilesDropped(const QList<QUrl> &urls)
{
    if (isClosing())
        return;
    const QStringList paths = urlsToLocalPaths(urls);
    if (paths.isEmpty() || !connected)
        return;

    const QString remoteBase = ui->remotePathCombo->currentText();
    QList<TransferBatchItem> items;
    QSet<QString> reservedUploadTargets;
    QStringList duplicateUploadTargets;
    for (const QString &path : paths)
    {
        QFileInfo info(path);
        if (!info.exists())
            continue;
        const QString remotePath = QDir::cleanPath(remoteBase + "/" + info.fileName());
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
        requestRemoteUpload(items.first().sourcePath, items.first().destinationPath);
    else if (items.size() > 1)
        requestRemoteUploadBatch(items);
}


void FileTransferWindow::onRemoteFilesDropped(const QList<QUrl> &urls)
{
    if (isClosing())
        return;
    const QStringList paths = urlsToLocalPaths(urls);
    if (paths.isEmpty() || !connected)
        return;

    const QString remoteBase = ui->remotePathCombo->currentText();
    QList<TransferBatchItem> items;
    QSet<QString> reservedUploadTargets;
    QStringList duplicateUploadTargets;
    for (const QString &path : paths)
    {
        QFileInfo info(path);
        if (!info.exists())
            continue;
        const QString remotePath = QDir::cleanPath(remoteBase + "/" + info.fileName());
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
        requestRemoteUpload(items.first().sourcePath, items.first().destinationPath);
    else if (items.size() > 1)
        requestRemoteUploadBatch(items);
}


QString FileTransferWindow::createRemoteDragTempDir() const
{
    const QString path = QDir::temp().absoluteFilePath(QStringLiteral("airan-remote-drag-%1-%2").arg(remote_id, m_instanceId));
    QDir().mkpath(path);
    return path;
}
